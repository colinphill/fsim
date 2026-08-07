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

## Conformance evidence and provenance

The v1 conformance corpus is original fsim test code with explicit behavioral
cross-references; permissively licensed public suites are references, not
implicit source imports. Reviewed source identities, pinned commits, license
decisions, excluded source classes, and the no-import boundary are recorded in
[`v1-conformance-audit.md`](v1-conformance-audit.md). Exact upstream bytes may
enter the repository only through a separately reviewed third-party inventory;
the IEEE-P1076 package snapshot remains the sole such tree.

Each independently authored expectation has a unique adjacent
`FSIM-CONFORMANCE` marker naming its reviewed source ID and bounded expected
outcome. The machine-readable
[`v1_conformance_corpus.txt`](../tests/feature_matrix/v1_conformance_corpus.txt)
maps every marker-bearing fixture to one registered CTest and its evidence
modes. `fsim.v1-conformance-corpus` derives the exact marker set from source,
checks its pinned digest, provenance and ownership, then requires the complete
frontend/elaboration, interpreter/LLVM O0/O2, cache, debugger, callback,
normalized-VCD, source-map, portable-path, ABI/API, plug-in, and tool evidence
union. The SimIR interpreter remains the runtime oracle; compiled, debug, cache,
callback, and trace results are differentials against that reference rather
than independent semantic definitions.

## Compilation pipeline

| Stage | Responsibility | Vertical-slice status |
|---|---|---|
| Source manager | Files, source locations, include and macro ancestry | Exact ordered compilation-unit/transitive snapshots, owning VHDL/SV/SystemC source records, and interned include/macro ancestry are current |
| Language frontend | Tokenization, preprocessing, parsing, name/type rules | Hand-written bounded VHDL and SV parsers, a multi-root SV preprocessor, and complete owning typed HIR for the v1 profile are current |
| Design elaboration | Candidate resolution, specialization, hierarchy, bindings, drivers, stable IDs | Recursive VHDL/SV/SystemC hierarchy, configurable complete-scope logical-library resolution with explicit overrides and lazy read-only mapped libraries, dense instance-specific specialization records, bounded scalar VHDL generic and integral SV parameter specialization, executable conditional/iterative/selection generate expansion, construction-actual transfer across all three languages, port aliasing, strength/charge provenance, and boundary checks are current; general generic/parameter typing remains planned |
| SimIR lowering | Explicit reads, writes, waits, branches, assertions and yields | A typed executable subset is current |
| Reference engine | Execute any supported SimIR with deterministic scheduling | Current |
| LLVM engine | Compile each design-unit specialization and execute via ORC | The application groups eligible processes from each bounded elaborated specialization into one LLVM module while retaining typed per-process interpreter fallback; update/delayed writes plus dynamic/static sensitivity waits are current |
| Runtime | Time, deltas, resolution, callbacks, force/deposit and diagnostics | Scheduler, domain-preserving process-owned driver slots, exact nine-state `std_logic`, wired and strength-aware four-state Verilog resolution, cycle-safe transmission graphs, `trireg` retention/decay, committed value changes, deposit, and force/release masking are current |
| Visibility | C API, debugger safe points and VCD | Executable session API, VCD, and a scope/signal-oriented REPL with source/time/signal breakpoints, all four step modes, and bounded packed process-local reads are current; complete local scopes/types are planned |

The language-specific HIR retains resolved symbols, types, overload choices,
constant values, and legality results. The common `DesignIR` owns dense
stable IDs for libraries, units, specializations, scopes, instances, processes,
signals, ports, drivers, source locations, and debug-visible objects. The
remaining legacy elaboration value projection is contained at one lowering
ingress; durable build and execution consumers use the explicit boundaries.

### Batch 126 boundary audit

The Batch 126 audit fixed the migration boundary to the live implementation,
rather than treating the names HIR and DesignIR as evidence that the layers
already existed. Its starting baseline was:

| Boundary | Current ownership and evidence | Release-blocking gap |
|---|---|---|
| Parser output | `frontend::ParsedDesign` owns a common 1,588-line value tree for both languages. Its declarations, expressions, statements, types, generates, callables, and source spans already retain much of the bounded syntax needed by semantic analysis. | The same records mix unresolved spelling, resolved/folded fields, elaboration-only substitution markers, and language-specific semantics. There is no immutable syntax-to-semantic handoff or distinct VHDL/SV HIR owner. |
| Semantic analysis | Project checking merges parsed units and injects reviewed standard-library views. Build selection then mutates the same `ParsedDesign` for delay alternatives and project-time normalization. Elaboration copies `DesignUnit` values per occurrence and mutates those copies for package/type/callable specialization, generate expansion, and name resolution. | A checked project cannot be treated as immutable semantic input. Legality and resolution results are implicit in mutated fields and control flow rather than explicit HIR records. |
| Semantic identity | Frontend declarations, scopes, types, expressions, and statements have no stable IDs. Units are found by kind/library/name, nominal types use strings and source-derived spellings, and the hierarchy/lowerer use transient pointers into parsed or specialized values. The elaborator internal surface contains 112 explicit frontend pointer/reference declarations. | Identity depends on spelling, vector order, source offsets, and object address during a build. It cannot be serialized, reordered, or compared independently of container lifetime. |
| Source provenance | The common frontend tree contains 68 `SourceSpan` occurrences with logical and physical file names plus offsets. Preprocessor tokens retain macro expansion stacks, but ordinary HIR nodes retain only `SourceSpan`; expansion ancestry normally survives only when a parser diagnostic copies it immediately. Semantic source dependencies are path strings. | Every semantic and DesignIR node needs an owning source-file/span identity, including macro/include ancestry and generated/specialized origin, without duplicating file strings or losing physical ownership. |
| Elaboration and DesignIR | `ElaboratedDesign` owns dense runtime signal, process, string-object, container-object, protected-object, specialization, and SystemC mappings. Lowering writes SimIR directly while hierarchy is constructed. | The public elaborated representation still has 47 direct `frontend::` references and lacks explicit library/unit/scope/instance/port/export/driver/type/source identities. There is no inspectable typed DesignIR before SimIR emission. |
| Downstream consumers | Frontend types appear 2,546 times across 80 elaboration implementation files and 145 times across eight application files. Compiler and runtime implementation files have zero frontend references and already consume SimIR or compact metadata. | Migration must converge at elaboration/application without leaking language nodes into compiler or runtime. Cache, debugger, VCD, diagnostics, and API projections need stable IDs before frontend storage can be retired. |

The migration proceeded additively. A language-neutral semantic core owns
typed IDs plus source-file, span, expansion, and origin tables. VHDL HIR and
SystemVerilog HIR adopt those identities while preserving the value-owned
parser result as a temporary lowering adapter. Explicit DesignIR owns hierarchy
and executable semantic metadata. Application/cache/debug/VCD/API consumers
now use stable projections; the remaining compatibility payload is explicitly
named the runtime adapter.
At every step the existing SimIR interpreter remains the oracle, compiler and
runtime stay frontend-free, and deterministic IDs are assigned from canonical
source/declaration and realized-hierarchy order rather than host addresses.

The shared semantic layer implements that identity contract in
`fsim::semantic`. Strong, non-interchangeable dense IDs cover source files,
spans, expansion frames, origins, libraries, units, scopes, declarations,
types, values, expressions, statements, instances, ports, and drivers. The
owning tables cover file/span/expansion/origin provenance plus units,
root scopes, declared types, values, and instances. Records own their strings;
references use IDs and never parser addresses. Files, expansion chains, and
exact spans are interned, while semantic occurrences remain distinct.

`check_project` constructs this model after deterministic source merging and
standard-library injection. Root and transitive input digests enter in manifest
and first-use order; units remain in canonical source order; declarations use
physical source offset with stable category/index tie breakers. A preassigned
dense type-ID range permits forward local type references without pointer
fixups. VHDL identifiers use their case-insensitive canonical key and
Verilog/SystemVerilog identifiers remain case-sensitive. The temporary
conversion adapter reads `ParsedDesign`, but its result remains valid after
the parse tree is cleared. Parsed source spans own preprocessor token expansion
stacks; HIR conversion interns their complete parent chains. SystemC
translation-unit roots enter the same source table with normalized generic
paths and exact content digests.

The VHDL declaration/type layer is now a separate owning
`semantic::vhdl::Hir`. It uses the shared unit, scope, declaration, type,
value, expression, instance, span, and origin IDs while owning all remaining
names and profiles. Units retain context clauses and architecture/entity
relationships. Declarations cover generics (including type, subprogram, and
package profiles), ports, signals, variables/files, aliases, overloadable
functions/procedures, generic templates and instances, package instances,
components, and generated declarations. Callable, component, protected, and
generate regions receive explicit nested scopes.

VHDL type definitions distinguish aliases and subtypes from enumeration,
array, record, access, file, protected declaration/body, physical, and scalar
forms. Subtype indications own type marks, optional resolution names, concrete
or expression-backed constraints, signedness, and indefinite-array state.
Enumeration literals, record fields, every array dimension, designated/element
subtypes, protected members, physical units, and the applicable predefined
type attributes remain explicit. Component/configuration binding aspects and
generic/port associations are likewise owning HIR records. Recursive generate
regions retain their lexical scope, declarations, instances, nested regions,
block interfaces, and association aspects. Expression-bearing fields refer to
stable expression slots populated by the complete typed expression and
statement payloads without changing their identity.

The VHDL executable HIR now fills those identities with owning expression,
statement, and process records. Expressions retain literal/operator/name/call/
selection/aggregate kinds, ordered operands, named actuals, aggregate choice
expressions, decoded strings, nominal typing, and scope-resolved declaration or
overload candidates. Every process, callable body, protected method, generic
subprogram template, concurrent statement, and generated body is traversed;
procedural block locals receive their own lexical scope and stable declaration
and value IDs.

Sequential and concurrent statements explicitly distinguish signal and
variable assignment, conditionals, selections, loops and loop control,
returns, procedure associations, waits, assertions/reports, blocks, and null
statements. Signal assignments own complete ordered waveforms, disconnect or
`unaffected` state, inertial/transport mode, rejection limits, and exact delay
alternatives. Process sensitivity items, call actual spans, case choices,
aggregate associations, and report/severity expressions are independently
source-addressable. VHDL TextIO/file, protected-method, access/allocation, and
attribute operations remain ordinary resolved call/name expression records,
so downstream lowering no longer needs a parser node to distinguish their
callee and operands.

The SystemVerilog declaration/type layer is likewise an owning
`semantic::sv::Hir` joined to the common semantic model only by stable IDs.
Compilation units retain module, package, and interface identity together with
time unit/precision, `default_nettype`, cell state, imports, and exports.
Declarations cover value and type parameters, local parameters, typedefs,
ports, nets, variables, functions, tasks, modports, enumeration literals, and
generated declarations. Callable profiles retain formal declarations,
automatic/static lifetime, return type, nested scope, locals, and bodies;
interface ports retain their interface type and modport rather than flattening
them to an unresolved spelling.

SystemVerilog types explicitly distinguish packed integral, enum, packed or
unpacked struct/union, dynamic array, bounded queue, associative array, static
array, string, alias, and type-parameter forms. Packed and unpacked dimensions,
signedness, queue bounds, associative index types, member offsets, and enum
literal values remain independently source-addressable. Generate regions own
their scopes, declarations, instances, process identities, concurrent
statements, and nested alternatives. The bounded v1 profile has no classes;
unsupported class syntax is rejected before HIR construction. Expression and
statement fields use stable identity slots filled by the owning executable
SystemVerilog HIR layer without retaining parser storage.

That executable layer preserves every bounded expression form, including
selected/indexed values, casts and calls, concatenation/replication, decoded
strings, and positional, named, keyed, or defaulted assignment-pattern and
call associations. Empty named actuals remain explicit instead of collapsing
operand positions. Statements distinguish blocking, nonblocking, and
continuous assignment; compound/prefix/postfix updates; force/release;
conditionals, qualified case forms, loops, task calls, and returns; delay,
event, and wait controls; event triggers; fork/join variants and process
control; assertions; formatted display/monitor/file operations; memory
transfers; container methods; and finish/pause/null forms. Lexical block and
fork scopes own their local declarations through stable IDs.

Initial, final, always, `always_ff`, `always_comb`, and `always_latch`
processes retain their exact kind, declarations, sensitivities, and statement
roots. Callable bodies and generated alternatives share the same executable
records, including source-spanned selection choices and delay expressions.
System-task formatting policy, file handles, memory radix/direction/bounds,
container receivers and call operands remain explicit metadata, so later
DesignIR construction does not need to reinterpret parser nodes.

`semantic::design::DesignIr` is now the owning elaborated boundary constructed
once after legacy elaboration and retained by `BuiltProject` beside its semantic
model. Dense, non-interchangeable IDs cover realized specializations, instance
occurrences, objects, process occurrences, sensitivities, transactions,
conversions, and external boundaries; ports and drivers use the shared semantic
ID space. Every internal relationship is ID-only and validates independently,
and every source unit/scope/declaration/value/process/span/origin relationship
validates against the retained model. Runtime numeric indices and native
SystemC handles remain explicit adapter locators, never implicit identity.

Realized HDL specializations own their canonical unit, instance occurrence,
typed parameter identity values, callable declarations, objects, and processes.
Hierarchy records retain parent occurrence and source instance separately, so
one source declaration can realize multiple stable occurrences. Packed signals,
strings, containers and slices, protected objects/members, aliases, and ports
have occurrence-specific object IDs; process records own static sensitivities,
drivers, and one explicit transaction descriptor per driven region. Boundary
conversion records link formal/actual objects and adapter processes without
frontend references. SystemC modules, ports, events, primitive channels,
signals, exports, processes, native handles, and writable-export policy enter
the same object/port/process/boundary tables, including SystemC-to-HDL children.

The compact elaborated design now assigns a dense specialization ID to every
instantiated unit occurrence and records its canonical unit identity, instance
path, directly owned process IDs, and canonical bounded VHDL generic or
SystemVerilog parameter/localparam values. Those values distinguish occurrence
and native-cache identity. Typed bounded scalar construction values also cross
SystemC factory boundaries in both directions. This older representation is
retained only as the execution-payload compatibility adapter.
`Simulation::runtime_adapter()` names that role explicitly; stable identity,
hierarchy, provenance, and public metadata come from `Simulation::design_ir()`
and `Simulation::semantics()`.

The Task 8 migration contains parser storage at the lowering ingress.
`build_project` moves the checked parser workspace into one temporary owning
adapter, applies delay-mode selection and time normalization there, then
reprojects the semantic model and both language HIRs from that exact normalized
input. Legacy elaboration may use addresses within the temporary value owner
while it runs, but the owner is destroyed immediately after its diagnostics and
SimIR payload have been copied. DesignIR construction, cache creation,
interpreter/JIT setup, debugger, VCD, Tcl, and C API therefore cannot retain an
address or view into the parser workspace.

Every build validates the compatibility payload against DesignIR before it can
escape that boundary. The validator proves one stable projection for each HDL
specialization, dense signal and signal alias, container alias, runtime process,
mixed conversion, SystemC instance, and named SystemC object/process. Compiler,
runtime, and API implementation files contain no frontend references. LLVM
module grouping and identity use DesignIR specialization/process IDs; the
interpreter sees only SimIR plus explicit runtime indices. Cache schema
`fsim-specialization-provenance-v5-designir` hashes semantic source ownership,
typed parameters, stable SystemC instance/object/boundary mappings and writable
exports while excluding transient native handles. Debugger, trace, Tcl, and C
API hierarchy/path/source metadata likewise traverse DesignIR, fetching rich
SimIR execution payload only through the explicitly named runtime adapter.

The bounded VHDL package path represents a constant-only package as its own
library unit. An explicit `use library.package.all` or
`use library.package.constant` clause resolves the project package, evaluates
its scalar integer/Boolean/bit constants in declaration order, and injects
folded immutable names into the consuming entity/architecture specialization.
The imported package source is recorded as a semantic source dependency, so a
package-only edit changes the owning specialization's native-object key even
when the executable unit source is unchanged. Cross-library dependencies are
associated with their own source set, and its language, standard, library,
compilation-unit policy, defines, and include settings enter the provenance
key. Package visibility may recurse through other constant-only project
packages; the full acyclic source closure is retained, while a visibility cycle
is diagnosed with its package chain. General project package bodies,
types/subprograms, and full VHDL visibility remain future semantic-layer work.
The official IEEE-P1076 `1076-2019` source snapshot is retained
byte-for-byte under `third_party/ieee-1076-2019` with its Apache-2.0 license,
authors, exact checksums, and dependency-ordered inventory. Bundling is not
activation: reviewed stages enter analysis individually. An explicit
`ieee.std_logic_1164` use currently activates the checksum-pinned declaration
and body through an intrinsic package projection that retains fsim's existing
nine-state type and operator identity. Compiler-supplied roots are stored
separately from manifest roots so source counts and manifest/cache alignment
remain stable; their exact path, bytes, compilation-unit digest, and semantic
dependency identity still enter design and specialization cache keys. The
native-cache standard-library version changes whenever a reviewed package
profile changes.

The numeric package stage uses the same projection and provenance boundary.
`numeric_std` records an explicit standard-logic dependency; `numeric_bit`
selects two-state signed/unsigned types while `numeric_std` selects nine-state
types. Their bounded conversion, resize, shift, and rotate calls lower directly
to the existing typed SimIR resize, copy, arithmetic, and shift operations, so
the interpreter and LLVM backends do not carry a parallel package evaluator.
Result-size and integer-width checks occur before execution, while runtime
integer range checks retain the existing deterministic failure path.

The fixed-point stage extends that boundary through `math_real`,
`fixed_float_types`, `fixed_generic_pkg`, and `fixed_pkg` in deterministic
dependency order. A constrained `ufixed` or `sfixed` remains an exact packed
nine-state value; its declared descending range supplies the binary-point
position, so no parallel runtime value kind is needed. Static integer
conversion performs checked 64-bit scale alignment and saturation during
lowering. Resize uses typed copies and existing arithmetic/shift operations for
scale alignment and bounded unsigned nearest rounding. Equal-range addition,
subtraction, comparison, and slices reuse the common packed kernels, preserving
interpreter/LLVM and cache identity.

The floating stage extends the same dependency graph through
`float_generic_pkg` and `float_pkg`. The default reviewed v1 profile is
binary32 only: a constrained `float(8 downto -23)` is stored as its exact
32-bit nine-state encoding. Locally static conversions, arithmetic,
classification, comparisons, and exceptional constructors are evaluated
during lowering and emitted as ordinary typed constants. Thus both engines
consume identical bits, native cache keys retain the complete package source
closure, and no platform floating-point ABI enters SimIR or the JIT boundary.

Reusable VHDL contexts may activate the reviewed package set once and expose
it to later entity/architecture units. Injection follows the fixed dependency
order before manifest analysis while keeping compiler-supplied and manifest
source inventories separate. Fully qualified references to declarations in an
intrinsic package are validated against its reviewed declaration inventory,
add that package's complete semantic source closure, and remain qualified until
the matching intrinsic lowerer selects the overload. This also preserves the
distinct two-state `numeric_bit` and nine-state `numeric_std` type views when
both libraries are visible.

Standard-logic mapping functions expand during lowering into exact scalar
state comparisons and conditional selections, then concatenate in declared
runtime order. This keeps all nine-state table behavior in the already
validated packed-value kernels and LLVM lowering. Static binary/octal/hex
string conversions become ordinary string constants; unsupported dynamic
profiles fail before SimIR emission. The declaration-only
`std_logic_textio` compatibility package records its dependency and aliases
without duplicating the common TextIO runtime.

The v2 VITAL stage uses a separate clean-room intrinsic revision,
`ieee-vital:2000:fsim-clean-room-v1`. Direct or context-expanded use of
`ieee.vital_timing` and `ieee.vital_primitives` injects fsim-owned virtual
package sources after `std_logic_1164`; no externally licensed VITAL body is
copied into the distribution. Typed package metadata supplies exact transition,
delay, map, fixed-vector, and truth-table declarations. Static composite
constants and generics retain their complete packed value and nominal identity
in specialization/cache provenance. Combinational VITAL calls lower to
ordinary wide packed SimIR operations. Timing checks use one typed operation
per call site with persistent runtime state keyed by instruction identity,
append-only compiled callbacks, exact nine-state edge masks, and ordinary
delayed signals for test/reference delay. Skew checks schedule their standard
Trigger signal at the earliest outstanding directional deadline. State tables
expand to packed comparisons and selections; variable profiles update
caller-owned previous-input storage while signal profiles use exact runtime
last-value history. The three path-record families and their unconstrained
arrays retain 64-bit project-tick delays, Boolean conditions, nominal record
layout, static choices, and null ranges. `VitalSignalDelay`, all three
`VitalWireDelay` overloads, and all three `VitalPathDelay` procedures lower to
one typed operation per call site. The kernel selects the shortest remaining
sensitized path, applies 01/01Z transition and output-map rules, and uses the
common projected-waveform scheduler for `OnEvent`, `OnDetect`,
`VitalInertial`, and `VitalTransport`. Scheduler-owned per-call glitch state
tracks the prior source, projected time/value, and detection time without
putting mutable scheduler state in generated code. Interpreter, LLVM,
artifacts, debugging, callbacks, native caching, and tracing therefore share
one deterministic path.

The clean-room `ieee.vital_memory` projection adds the standard memory,
port-flag, table, violation, timing, and schedule types without importing an
external package body. One resource-governed runtime model supplies arbitrary
logical depth through a default word plus sparse materialization, with a
contiguous fast path for small memories. Word/subword tables, multi-port
contention, violations, vector setup/hold and period/pulse checks, and all
memory path-delay profiles retain independent typed state. Memory paths
normalize scalar/vector and single/01/01Z/01ZX overloads into cross, parallel,
or subword candidates before handing per-bit transport waveforms to the common
projected scheduler. Static load files are validated and embedded in the
serialized operation so standalone relocated artifacts do not reopen build-
tree inputs; dynamic filenames retain the confined runtime file service.
Declarative VITAL_LEVEL attributes are accepted as non-executable metadata in
entity, architecture, and generated regions, allowing configured vendor-style
cell and memory models without vendor-name special cases. Native cache v78
hashes embedded contents and every selected memory/path input.

Executable VHDL units and package declarations may also name a constant as
`package.constant` in their own library or
`library.package.constant` explicitly. Elaboration collects those qualified
identifiers from types, declarations, generates, instances, and executable
statements, specializes only the referenced packages, and injects folded
qualified constants before ordinary substitution. Consequently, an unrelated
package does not enter that unit's semantic source closure or native-cache key.

The bounded VHDL context path represents a reusable context declaration as a
library unit containing library clauses, use clauses, and references to other
project contexts. A `context library.name;` item is expanded recursively before
package visibility resolution, so a context may expose constants through an
acyclic context/package chain. Context sources join that chain's semantic
source dependencies, and missing, malformed, or cyclic references are
diagnosed before specialization.

The bounded SystemVerilog package path represents integral package parameters
and localparams as immutable declaration-ordered constants, plus packed
integral `typedef` aliases whose ranges may depend on those constants.
Bounded `typedef enum` declarations use an explicit packed `bit`, `logic`, or
`reg` base; their explicit or implicit enumerator values enter the same
declaration-ordered constant environment and are checked for base-width fit
and duplicate values during specialization.
Bounded packed structs and unions contain non-aggregate `bit`, `logic`, or
`reg` members. Parameterized member ranges are specialized before layout.
Struct layout maps the first member to the most-significant bits and the last
member to the least-significant bits. Union members must specialize to the same
width and all map to offset zero, so writes through one member are immediately
visible through every other member. Member reads/writes lower to the common
SimIR `Extract`/`Insert` and sliced-write operations, retaining
interpreter/LLVM semantic equivalence without a backend-specific aggregate ABI.
One constant bit- or part-select may follow a member name. Parameter and
imported-package-constant bounds are folded after specialization, then the
member-relative offset is composed with the aggregate layout offset before
lowering.
SystemVerilog constant indexed part-selects normalize `base +: width` and
`base -: width` into a direction-preserving conventional range. Both ascending
and descending declarations therefore reach the same contiguous SimIR
extract/insert operations, while nonpositive widths and out-of-range endpoints
are rejected before lowering.
Runtime-base reads use a distinct fixed-width `DynamicPartSelect` operation.
It retains the signed 32-bit base, declared bounds/direction, selection
direction, result width, and two-/four-state policy. The interpreter and LLVM
map each result bit independently, so partially out-of-range reads produce X
or zero only for the unavailable bits and an unknown base produces an all-X
or all-zero result. Dynamic procedural part-select targets are intentionally
not admitted by this read operation.
SystemVerilog replication concatenations require a positive specialized
constant count and a statically sized nonempty operand group. The lowerer
builds the repeated value with binary doubling, requiring logarithmically many
two-operand `Concatenate` operations rather than materializing one operand per
copy. Expanded widths are checked against the SimIR limit before allocation.
Bounded integral streaming concatenations first lower their operand list to
one ordinary packed concatenation. Right streams retain that order; left
streams extract constant-sized chunks from the least-significant edge and
concatenate them in extraction order, including a narrower final chunk. The
same algorithm is implemented in the typed constant evaluator without shifting
by 64, while dynamic slice sizes and aggregate/container operands fail before
runtime lowering.
SystemVerilog `<<<` uses the logical-left kernel. For `>>>`, elaboration
selects arithmetic-right only when the left operand is signed; unsigned
operands retain logical-right behavior. Arithmetic right shifts replicate the
four-state sign bit, including `X` and `Z`, and an oversized shift fills the
entire result with that bit. The interpreter supports wide packed values and
the LLVM path emits equivalent signed shifts for values up to 64 bits.
Compilation-unit and unit-local `import package::*` or
`import package::name` clauses inject case-sensitive direct constants and
types, while `package::name` remains explicitly scoped. Alias chains resolve
before occurrence specialization, so package- or module-parameter-dependent
ranges remain distinct per specialization. Imported packages may themselves
import packages; elaboration detects package cycles, typedef cycles, missing
types, and ambiguous wildcard names. Only recursively referenced package
sources enter specialization and native-cache provenance.

The Verilog-2005/SystemVerilog constant evaluator folds `$clog2` with exactly
one nonnegative integral argument. Zero and one produce zero; larger values
use the ceiling of the base-two logarithm without floating-point arithmetic.
Evaluation occurs after ordered parameter substitution, so a derived
localparam can size ports, signals, generated declarations, and cache-distinct
specializations. Arbitrary vector-valued and negative arguments remain
pending until constant evaluation retains their complete self-determined
width and unsigned interpretation.

The current hierarchy builder recursively follows direct VHDL/SV instances,
bounded conditional/iterative/selection generate regions, and always-selected
static regions representing unguarded VHDL blocks or direct/named
SystemVerilog generate contents.
Generate conditions, loop controls, selectors, and choices are
constant-evaluated after generic/parameter substitution for each occurrence.
Selected VHDL units retain generated regions until enclosing value, type,
subprogram, and local-package specialization is complete. A block with
non-value interface generics is then specialized as a temporary declarative
unit at its exact expansion point: package formals bind first, type and
subprogram formals bind against the enclosing visibility, and value/port maps
expand only after those bindings have resolved dependent types. Scoped block
binding identities and callable/package source dependencies join the owning
specialization key before native-cache lookup.
Only selected branches/alternatives and realized iterations enter DesignIR.
Declared block/alternative labels become stable path components; loop
iterations use the common `label[index]` spelling so explicit mixed-language
bindings do not depend on source-language hierarchy syntax.
VHDL selection choices retain an optional directed upper bound. Elaboration
normalizes every non-null inclusive range to a signed 64-bit interval, detects
interval/scalar overlap without enumerating its values, and treats a
directionally null range as selecting no value.
SystemVerilog generate-loop initialization may declare an inline `genvar` or
refer to a module-scope declaration. The frontend normalizes `i = EXPR`,
prefix/postfix `++`/`--`, and `+=`/`-=` into the same explicit next-value
expression consumed by bounded constant elaboration and stall detection.

Selected generated bodies may own packed local signals, concurrent
assignments, processes, instances, and nested regions. Specialization flattens
that content into the owning unit while qualifying only locally declared names
with the generated scope. Parent references remain unchanged, process-local
variables shadow generated signals correctly, and unselected body content
never receives a signal or process ID.
Generated bodies may also declare bounded scalar/integral constants. Each
body extends its parent's constant environment in declaration order before
signal types, behavior, child actuals, and nested generate controls are
substituted. Constants therefore consume no runtime storage, while a loop body
reevaluates index-dependent declarations independently for every iteration.
Selected VHDL generate bodies additionally retain bounded array, enumeration,
record, and subtype declarations. Named-type resolution merges types and
objects by physical source offset so an earlier object cannot see a later type;
after selection, constraint substitution uses the branch-local constant and
iteration environment. Expansion qualifies every realized type name beneath
the exact branch or `label[index]` scope and extends its source declaration
identity with that scope, keeping equal declarations from different generated
instances nominally distinct without losing source provenance.
Generated VHDL function and procedure declaration/body pairs are merged after
type specialization, while physical-source ordering limits each callable body
to earlier overloads and its own designator. Ordinary and generic callables are
qualified beneath the selected branch or iteration. Generic templates and
`is new` instances are materialized once more after generate expansion, with
the internal scoped template name admitted only when it names an exact retained
generated declaration. The resulting instance name and generic binding join
specialization identity and source provenance before lowering and native-cache
lookup.
Generated local generic-package instances follow the same two-phase rule.
Parsing retains them in `GenerateBody`; selection qualifies the instance and
its map under the exact branch or `label[index]` scope, and post-expansion
package materialization publishes selected constants, types, and callables.
Lexical generated-package prefixes are excluded from pre-expansion external
package discovery, and the realized package binding participates in
specialization and native-cache identity.
VHDL conditional, iterative, and case-alternative bodies retain their
declarative part separately from concurrent statements; any nonempty
declarative part requires the grammar's separating `begin`. Expansion applies
the declared branch or alternative label, or the concrete `label[index]`,
before qualifying its local signals and behavior.
An unguarded VHDL block or named SystemVerilog `begin : label` static region
contributes its label as a stable hierarchy component. Direct items inside an
explicit SystemVerilog `generate` region use one empty static parent scope, so
they retain module-scope names while their nested generated regions inherit
the parent's constant and object environments.
Unqualified children resolve from an immutable per-build candidate index. Each
lookup lazily queries the parent logical library followed by first occurrences
from `[elaboration].search_libraries`, or the ordered replacement supplied by
repeated `--search-library`. All queried libraries form one ambiguity scope;
library order never hides a later collision. Missing or ambiguous diagnostics
therefore retain the ordered scope and canonical identities considered. An
unavailable configured library remains inert until an unqualified lookup needs
it. A manifest
binding may override an instance with a language-qualified VHDL or SV target;
the builder then connects named or positional whole-signal actuals by aliasing
the child port ID to the parent signal ID. It diagnoses missing or duplicate
connections, width and signedness mismatches, implicit loss into a 2-state
destination, recursive hierarchy, unused bindings, and unresolved multiple
boundary drivers. Bounded VHDL generics and SystemVerilog parameters are
specialized before port checks, including when an explicit binding changes the
child language. Positional actuals map by declaration ordinal. A named actual
uses case-insensitive matching whenever either the association syntax or the
target declaration is VHDL; a VHDL name that would match multiple distinct
case-sensitive Verilog/SystemVerilog parameters is rejected as ambiguous.
The association syntax owns ordering legality, so VHDL may use positional
actuals followed by named actuals while Verilog/SystemVerilog may not mix the
two forms. Typed scalar construction actuals also cross SystemC factories in
both directions. Expression port actuals and unpacked/record boundaries remain
outside this slice.
Exact nine-state `std_logic` and four-state `sv_wire` resolution are current
in the reference runtime; exact nine-state generated code remains a
capability-gated interpreter fallback.

Verilog user-defined primitives are a distinct candidate kind rather than
top-selectable modules. The immutable candidate index resolves each UDP
instance by case-sensitive logical-library identity before terminal checks;
built-in gates never enter that lookup. One normalized `UdpTableInfo` per
selected declaration owns its canonical `udp:library.name` identity, ordered
rows, terminal profile, sequential state, and SHA-256 digest. Instance
specialization provenance carries both identity and digest, so generated,
multi-root, searched-library, and mixed-language wrapper paths use the same
resolver and native-cache contract as ordinary hierarchy.

Verilog drive and charge strengths use one canonical rank model from frontend
HIR through portable units, specialization provenance, SimIR, runtime state,
and native-cache identity. Resolved nets compare independent zero/one
components per bit, retain exact ties as `X`, ignore high impedance, and keep
the wired-AND/OR policies distinct. Gate, tri-state, UDP, procedural,
cross-language, pull, supply, and implicit-pull contributions enter the same
resolver. MOS drivers derive their effective strength from the live source and
apply the standard resistive reduction.

Bidirectional `tran` families are passive topology edges rather than synthetic
drivers or hierarchy children. The scheduler traverses each enabled connected
component per lane with cycle detection, scalar/vector mapping, conditional
four-state conductance, and cumulative resistive reduction. `trireg` signals
retain the last driven value at their declared charge strength and use the
common checked inertial event queue for zero, finite, renewed-drive-cancelled,
or infinite decay. The topology, charge metadata, and selected provenance are
checked across `.fsimobj`, `.fsimdesign`, relocated `.fsimlib`, and native
cache boundaries.

Schema-2 projects may select an ordered list of aliased roots with repeated
`[[project.top]]` records or repeated `--top ALIAS=TARGET` replacements. The
application resolves the complete list transactionally before hierarchy
construction, then elaborates every selected HDL unit or SystemC factory into
one `ElaboratedDesign`. The root alias is the first canonical hierarchy path
component. One root retains the legacy collapsed C API root; multiple roots
are children of a synthetic `$root` scope. `ElaboratedDesign::top()` remains a
source-compatible view of the first alias while `roots()` is authoritative.
Ordered aliases and selected canonical identities participate in the overall
design key and native-cache provenance, so reordering roots cannot reuse the
wrong design.

All roots enter one deterministic scheduler and global tick domain. Their
initial processes, delta activity, timed events, terminal behavior, SystemC
lifecycle, VCD identifiers, debugger scopes, callbacks, and Tcl/C API objects
therefore share one session. Library candidate indexes, VHDL package and
configuration declarations, SystemVerilog package declarations, and SystemC
plug-in registries are build-global; instance signals, variables, processes,
and native module objects remain root-local. Before process lowering, the
elaborator specializes and allocates every SystemVerilog root's packed
root-level signal surface. That makes language-defined top-level hierarchical
references such as `glbl.GSR` independent of manifest order, including the
conventional separately selected vendor global-signaling module. Descendant
paths are deliberately not opened as an fsim-specific backdoor: a reference
such as `glbl.child.internal` is rejected with `FSIM-ELAB-ROOT-001` and must be
exposed through a root-level port or signal. VHDL and SystemC cross-root
communication likewise uses their defined ports, signals, packages, or common
kernel services rather than arbitrary foreign hierarchy shortcuts.

Schema-2 `[[library_map]]` records and repeated `--map-library
LIBRARY=DIRECTORY` replacements map a logical library to one read-only,
relocatable `.fsimlib` directory. A mapping is metadata-only until a qualified
top, explicit binding, visible VHDL library reference, or complete unqualified
search scope actually queries it. The loader then verifies canonical
`fsim-library.toml` metadata, declared dependency order, every selected payload
checksum, and each indexed unit identity before committing restored units.
Dependencies are logical names with explicit mappings; fsim never searches
neighboring host directories. Portable VHDL and SystemVerilog owning unit
graphs plus distinct `.fsimudp` declaration payloads restore directly into the
candidate index without invoking a preprocessor or parser. UDP payloads receive
the same checksum, identity, source-relocation, duplicate-input, and
transactional publication checks as ordinary units. Bundled logical source
names and optional source text
remain artifact-relative, so diagnostics, debugger breakpoints, VCD, and cache
identity survive moving the complete directory.

`fsim build --export-library LIBRARY=DIRECTORY` publishes through a sibling
staging tree and one directory rename, refuses replacement, and removes partial
staging state after failure. The final tree is read-only. All design caches,
recompiled SystemC images, LLVM objects, trace output, and debugger state live
under the consumer cache instead. Optional host-native variants are
accelerators only. A SystemC image requires exact runtime/SystemC ABI,
compiler, target, CPU policy, and content identities. An LLVM object also
requires the exact LLVM version, runtime/frame/result ABI sizes, target triple,
data layout, CPU, sorted feature set, optimization level, object key, and
checksum. LLVM exports compile optional objects from a temporary self-mapped
portable artifact, ensuring producer and consumer use identical relocated
source provenance. Any compatibility mismatch falls back to the portable unit
or bundled SystemC source; a compatible but corrupt payload is an integrity
error. Accepted native fingerprints and ordered metadata/unit/source identities
participate in cache provenance, while the mapped directory's absolute path
does not. Format-1 SystemC publication rejects producer-only include paths,
definitions, compiler/linker options, and external libraries rather than
publishing a bundled-source fallback that cannot reproduce the producer build.

Manifest-free execution uses two additional immutable directory artifacts.
`fsim compile` publishes one explicit VHDL, Verilog, or SystemVerilog
compilation unit as `.fsimobj`: canonical metadata indexes independently
checksummed relocated sources and portable owning units. Repeated objects load
in command order, preserve VHDL analysis dependencies and SV compilation-unit
isolation, and never reopen producer sources. `fsim elaborate` resolves one or
more roots through the ordinary candidate index and publishes `.fsimdesign`.
Its checksummed runtime, semantic, and DesignIR projections construct a
scheduler without entering a frontend or elaborator.

The design digest covers ordered object contents, selected identities,
bindings/search scope, timing/seed/optimization policy, runtime ABI, state
indexes, and specialization keys. It salts native module identity; LLVM's
host/ABI/options fingerprint remains the final cache boundary. Both artifact
trees install by a sibling staging rename, are read-only, and reject overwrite.
`fsim systemc compile` publishes one host-native C++20 translation unit as a
`.fsimscobj`. Its canonical metadata records the complete compiler dependency
closure, settings, toolchain/target/runtime ABI fingerprints, and object
checksum. `fsim systemc link` consumes an ordered object list and publishes one
logical-library `.fsimscplugin` after loading the image and transactionally
validating its sorted factory and parameter-schema inventory. Project builds
use the same content-addressed compile/link path, so editing one translation
unit does not rebuild its peers.

`fsim elaborate --systemc-plugin` merges those factory candidates with ordered
HDL objects. Format-2 `.fsimdesign` metadata embeds every selected plug-in and
its native provenance. Standalone loading checksum-validates the embedded
image, recreates factory roots and their native children, remaps serialized
handles by stable hierarchy path, and reconnects ports, events, signals,
processes, exports, lifecycle callbacks, debugger objects, and trace signals to
the common runtime. Designs containing SystemC are therefore relocatable only
between exact-compatible hosts; HDL-only format-1 and format-2 designs remain
portable. `fsim simulate` places LLVM objects and HDL file state in explicit
`--cache` and `--file-root` consumer directories and writes traces to the
requested path, never inside `.fsimdesign`. Delay selection is fixed by
elaboration. The additive C++ phase/inspection API does not change the v1 C
ABI.

The hierarchy is deliberately bidirectional for SystemC. An HDL instance
path may bind to a registered SystemC factory. During its elaboration, a
SystemC factory may mark a normally constructed child module as an HDL proxy;
the
manifest binding at that full path resolves the placeholder to a VHDL
architecture or SV module. The common elaborator remains authoritative in
both directions and supports recursive alternation between languages while
retaining one stable-ID namespace, one port-conversion policy, and recursion
detection. Facade modules declare these proxies with `SC_FSIM_HDL_MODULE` and
bind ordinary `sc_in`, `sc_out`, and `sc_inout` ports to signals or parent
ports. The hierarchy registry converts the marked native child into a
same-path HDL implementation descriptor while retaining module-and-port
identity; it does not add an `hdl_instance` level. Processes, lifecycle hooks,
events, channels, exports, signals, and nested modules are rejected within the
proxy. The older `fsim::systemc::hdl_instance` facade remains deprecated but
compatible, and the native ABI remains the implementation mechanism. Foreign
children can be created only during elaboration, never
dynamically after simulation starts. Native SystemC child modules are captured
recursively by the same factory root, and child ports may alias a direct
parent signal or port. Concrete typed signal-export chains resolve to that
same alias graph; standard signal-interface exports retain their own stable
hierarchy handles and paths while sharing the resolved dense signal ID. Each
constructed factory root owns independent lifecycle
state: the two elaboration callbacks run after common-object binding, the
start callback runs before the first kernel start, and the end callback runs
at terminal completion or session teardown.

`hdl_module::set_actual` records named signed scalar construction values on
the foreign-child descriptor through an append-only host callback. The common
elaborator applies them to the explicitly selected HDL unit using that target
language's name and subtype rules before port checks. They therefore flow into
the same canonical specialization and native-cache identity as source-written
generic/parameter actuals. The reverse HDL-to-SystemC direction now uses
factory-declared typed schemas and construction-value delivery.

Source factories are normally published with `SC_FSIM_EXPORT` or
`SC_FSIM_EXPORT_AS`. Each macro contributes a plug-in-local descriptor; the
support library owns the one initialization entry point, sorts descriptors by
public name, rejects duplicates before registration, and publishes an optional
`fsim_factory_parameters` schema discovered on the exported type. Multiple
translation units and multiple aliases of one type are supported. A
handwritten entry point remains the append-only compatibility path and cannot
be combined with macro exports in the same image.

The reverse-path ABI spine now exists: a factory registers ordered scalar
parameter declarations after its typed factory registration, the transactional
plug-in loader buffers and replays both as one unit, and the hierarchy registry
canonicalizes explicit/default values before invoking the module constructor.
An active constructor reads only declared values through
`construction_value<T>`. The schema supports integer, natural, positive,
Boolean, and bit constraints. The common HDL hierarchy walk evaluates source
instance actuals in the parent specialization, applies VHDL/SV association
rules, validates the schema, and invokes an application-owned provider only
after canonicalization. The constructed ports are then connected and checked,
so parameter-dependent SystemC interfaces never pass through a mismatched
default construction.

### SystemVerilog class object model

SystemVerilog classes are owning semantic declarations, not hierarchy design
units. The frontend retains compilation-unit, package, module, interface, and
nested declarations with canonical case-sensitive identities, forward
declarations, value/type parameters, bases and implemented interfaces,
declaration-ordered properties, methods, constraints, source spans, and
out-of-block definitions. A separate resolver completes visible class marks
and extern prototypes transactionally. Inheritance validation then checks the
single-base graph, interface use, member visibility, hiding, pure/final rules,
override profiles, and covariant class-handle returns before specialization.

Specialization evaluates class and base actuals, owns inherited and declared
property layouts, assigns stable virtual slots, and contributes all transitive
source and parameter identity to project and native-cache provenance. There is
no language-level object-count or container-length ceiling. Layout arithmetic
is checked against host-addressable storage; runtime heap, static-state,
container, recursion, and suspension limits are caller-supplied resource
budgets derived from materialized storage.

Each simulation owns one opaque class heap, one per-specialization static
store, and one method dispatcher shared by every root. Handle zero is `null`;
live handles encode a slot generation so stale identities cannot alias reused
storage. Construction publishes only after ordered base-to-derived steps
succeed. Object properties preserve declared and dynamic types, hidden base
members retain owner-qualified storage, and virtual calls select compiler-
assigned slots from the dynamic specialization. Method frames own `this`,
copy-in/copy-out actuals, automatic locals, guarded recursion, and resumable
task continuations. Fixed/dynamic arrays, queues, associative arrays, and
unpacked aggregates may contain checked opaque handles while packed placement
is rejected.

Source class expressions first resolve against canonical lexical and
specialization environments, then lower to owning class SimIR operations.
Interpreter execution invokes simulation-owned typed services directly. LLVM
O0/O2 code instead returns an append-only service-boundary status containing
only the operation index and next program counter; the common scheduler runs
the same typed service and resumes the native frame. Native objects and cache
keys therefore contain canonical class metadata but no host addresses.

The application exposes opaque object, static-state, and suspended-frame
inspection plus packed-property callbacks and trace snapshots, and schedules
class tasks through the common time/delta scheduler. Versioned class owning-
unit and runtime-state payloads preserve declarations, specializations,
executable bodies, initial static state, virtual slots, continuations, and
provenance through `.fsimobj`, `.fsimdesign`, and relocatable `.fsimlib`
artifacts.

Class constraints have a second, canonical semantic HIR keyed by exact class,
property, constraint, base, and specialization identities. It owns typed
bindings, source-order block composition and override provenance, visibility,
default modes, normalized expressions, `soft`, `inside`, weighted
distributions, implication, structured blocks, `foreach`, and solve-before
edges. Exact-specialization lowering creates finite typed variables and
validated clauses without retaining parser nodes or host addresses.

Each object owns independent random-property and constraint modes, revisions,
root/object/property/call stream identities, and portable `randc` cycle state.
The iterative finite-domain solver uses explicit variable, domain, search,
elapsed-work, and storage budgets. Object `randomize` and scope
`std::randomize` stage all scalar and materialized-container assignments,
callbacks, revisions, and cycle state and publish them atomically only after a
complete solution. Unsatisfiable calls return zero; malformed state, resource
exhaustion, or callback failure cannot partially mutate the heap or scope.
Seeds and exact domain permutations are host-independent, so interpreter,
LLVM service paths, artifacts, relocation, and cache reuse observe the same
result. Scope randomization currently uses the validated interpreter service;
its complete operation shape is nevertheless part of native-cache identity.

The debugger and trace API expose stable random property/constraint paths,
enable state, revision, stream seed, `randc` domain signature, cycle, and used
count without exposing an RNG object. A checksummed `sv-constraint-hir`
payload makes the semantic graph independently reloadable in standalone
designs; `.fsimobj` and mapped-library flows reconstruct the same graph from
portable owning units. Covergroups and UVM library/runtime behavior remain in
following closure batches.

### Concurrent assertion ownership and observation

SystemVerilog design units own append-only sequence, property, checker, and
concurrent-directive records independently of parser storage. Raw tokens retain
macro/source provenance while parallel typed records own formals, locals,
clocks, disables, references, temporal operators, endpoint observations,
actions, and region policy. Public semantic HIR assigns every concurrent
directive a stable explicit or synthesized name, source/origin identity,
Preponed/Observed/Reactive policy, observer policy, and deterministic coverage
slot.

The bounded executable scalar slice materializes an ordinary assertion process
per directive and instance. Engine-neutral hidden output markers cross the
interpreter/LLVM boundary without extending the SimIR ABI; the application
consumes them before user output, applies simulation-wide assertion controls,
updates per-instance attempt/pass/failure counts, and publishes stable
pass/failure/disabled events to a retained trace vector and exact callback.
Disabled samples do not increment coverage, while pass and failure actions can
be suppressed independently. The marker representation contains no host
address and survives object/design serialization, relocation, multiple roots,
and native-cache reuse. Unsupported executable forms reject before process
publication instead of disappearing silently.

### SystemVerilog functional coverage ownership and execution

SystemVerilog design units and classes own append-only covergroup declarations
independently of parser storage. Each declaration retains constructor and
procedural-sample profiles, an event or procedural trigger, instance and type
options, source-ordered coverpoints and crosses, exact tokens/spans, and stable
owner, declaration, specialization, and runtime identities. Resolution binds
lexical, package-qualified, and class-qualified coverage types, constructor and
sample actuals, explicit or implicit cross operands, and design-unit or
class-method sample calls before runtime state can be published.

One engine-neutral sampling transaction implements scalar, ranged, wildcard,
arrayed, automatic, default, ignored, illegal, and transition bins together
with coverpoint/bin guards. It applies ignore-before-illegal-before-regular
precedence, preserves overlapping transition progress, and constructs ordered
automatic or explicit cross tuples only after every input has been validated.
Instance-over-type weight, goal, threshold, merge, and per-instance policies
feed exact basis-point percentages and the same structured report tree used by
the deterministic text renderer.

Explicit, event-driven, and procedural samples share one scheduler and callback
sequence. Reentrant, mismatched-trigger, illegal-bin, hit-overflow, and resource
failures are diagnosed before partial publication. Debugger snapshots and
trace projections expose stable instance-qualified paths, aliases, meaningful
VCD-compatible values, source identity, time, and delta without host addresses.
The built-project coverage state owns declarations, instances, hits, transition
progress, exclusions, reports, callbacks, traces, and aliases. Owning-unit
schema 11 and required standalone coverage schema 1 preserve that state through
`.fsimobj`, `.fsimdesign`, mapped-library relocation, multiple roots, and cold
or warm LLVM O0/O2 reuse. Published declaration, bin, cross-product,
transition-work, transaction-input, and persistent-state limits bound all
static and runtime growth.

### SystemVerilog scalar and Unicode value model

SystemVerilog `shortreal`, `real`, `realtime`, `time`, and `chandle` are
distinct semantic and runtime kinds rather than aliases of the packed integer
plane. Decimal literals retain canonical spelling, literal family, and time-
unit metadata until contextual conversion. A shared folding service performs
deterministic IEC 559 binary32/binary64 conversion, exact tick scaling,
checked integral conversion, and kind propagation for dependent constants,
package imports, overrides, ports, callables, class members, and multiple-root
specializations.

The runtime scalar plane stores canonical binary32/binary64 payloads and exact
unsigned ticks. It owns checked arithmetic, comparison, classification,
rounding, formatting, scanning, and delay conversion; conversions publish no
partial result on overflow, nonfinite rejection, unsupported operations, or
resource exhaustion. Interpreter execution and LLVM O0/O2 both use the same
typed scalar services. Debugger mutation, callbacks, snapshots, and VCD use
the declared kind: real-family values use VCD real declarations while exact
time and opaque-handle identities use 64-bit vectors.

`chandle` is a nonnumeric, simulation-owned opaque identity. A generation-safe
registry publishes no host pointers and detects null, stale, and foreign
identities. Creation, aliasing, cleanup callbacks, storage accounting, and
debugger updates are transactional; cleanup runs at most once. Only null,
equality, inequality, and checked same-kind transport are executable until
the later DPI/VPI/VHPI batches attach standardized foreign ownership.

Mutable SystemVerilog strings use one strict UTF-8 service and index Unicode
scalar values, not encoded bytes. Length, iteration, slicing, replacement,
comparison, case conversion, substring search, integer conversion, and
real conversion share that representation and bounded allocation policy.
Invalid UTF-8, invalid Unicode scalar values, invalid indices, and expanding
operations that exceed a resource budget leave the prior value unchanged.
The same scalar/string/chandle profiles recurse through the supported static,
dynamic, queue, associative, packed-aggregate, and unpacked-aggregate value
shapes and through bounded scalar file I/O.

Portable owning-unit schema 8 and portable-library schema 5 preserve scalar
type identity, canonical literal payloads, and negative contextual operands.
The design-state codec preserves the corresponding executable signal,
callable, container, aggregate, string, and chandle state. Both codecs reject
invalid scalar enumeration values before publishing an artifact. Standalone
`.fsimdesign`, relocated mapped `.fsimlib`, and cold/warm/edited native-cache
flows therefore share producer-independent canonical identities.

This closes the scalar substrate needed by later language work, but it is not
UVM closure. Arbitrary-width packed values are closed by Batch 151 and the
remaining governed unpacked-data/file/procedural substrate by Batch 152. SVA,
and covergroups are closed by Batches 154-155; DPI/VPI/VHPI and the UVM
library/runtime retain their locked later-batch ownership.

### SystemVerilog unpacked data and procedural closure

Batch 152 extends the owning runtime value graph instead of flattening
unpacked values into host memory. Multidimensional prefix selections retain
their remaining rank and declared shapes; range and indexed slices adapt equal
dimension counts and snapshot overlapping copies before publication. Named
and anonymous unpacked structs/unions, strings, nested containers, and
assignment-pattern defaults remain recursively typed. Integral- and
strict-UTF-8-string-index associative arrays keep deterministic key order and
checked storage ownership. The same profile walker provides recursive
packing for admitted VHDL/SystemVerilog fixed-container ports and for bounded
aggregate memory-file transfers.

Runtime-valued integral and real delays carry a typed source register,
timeunit scale, and timeprecision quantum into `WaitFor`. General edge
expressions retain deduplicated signal dependencies and a four-state baseline;
runtime-selected force/release captures one checked declared-range offset.
Callable activations capture nonlocal ref selections once, nested static locals
bind by declaration identity, and simultaneous fork-site activations use
generation-safe process handles. Typed mailbox and semaphore handles own
bounded FIFO storage and waiter queues. Named-event wakeups and deterministic
container `shuffle()` share stable scheduler order and the process random
stream.

Cancelable scheduler handles carry an owner token and remain live only while
their task is pending. Execution, cancellation, discard, and owner destruction
invalidate them; foreign or moved-from schedulers cannot cancel work. Callback
failure restores the scheduler run state and leaves later callbacks resumable.
Runtime-state schema 16 and native-object schema 87 preserve and separate this
append-only operation set. Standalone designs, relocated mapped libraries,
debugger/callback/VCD paths, and cold/warm LLVM O0/O2 reuse validate the same
state before publication.

### SystemVerilog program, clocking, and virtual-interface model

Programs retain their own frontend, semantic, and SystemVerilog-HIR unit kind,
but reuse ordinary specialization, hierarchy, process, and artifact ownership.
Their executable distinction is explicit reactive process metadata. The
scheduler orders reactive work after active/inactive updates and before
postponed observation, and every resume path carries the owning process phase
instead of inferring it from a source construct at runtime.

Each clocking block materializes one event alias, active-region input sampler,
owned sampled storage, and output request/driver paths as required by its
members. Default and member skews become checked transport history or projected
driver delays; edge qualifiers select the sampler or driver sensitivity.
Procedural cycle waits reference the selected default clocking event and count
occurrences rather than converting cycles into ticks. A modport clocking member
forwards the same event and directional member aliases used by direct access.

Virtual-interface values are deterministic 64-bit simulation identities, not
host pointers. The hierarchy builder keys them by concrete interface instance
and canonical specialization, then records the selected modport view on every
forwarded alias. A restricted alias may preserve the same view but cannot widen
to a generic port or rebind another view. Portable Type archives serialize the
virtual marker, interface name, modport, and parameter actuals in one canonical
order, while enum and schema validation rejects malformed state before it can
become executable.

## Runtime values

The runtime distinguishes three logic domains:

- `Bit2`: `0` and `1`;
- `Logic4`: `0`, `1`, `X`, and `Z`; and
- `Logic9`: VHDL `U`, `X`, `0`, `1`, `Z`, `W`, `L`, `H`, and `-`.

Packed storage is used throughout. Scalar and common vectors up to 64 bits use
an allocation-free word fast path; arbitrary-width Logic4 values use the same
owning `PackedLogic4` abstraction with checked word-vector storage. Conversion
to a lower-state domain must be explicit whenever information could be lost.
The common packed value retains either `aval`/`bval` Logic4 words or four
ordinal Logic9 planes. It exposes checked `Logic4Word` and `Logic9Word`
representations for widths up to 64 elements, scans every word for lossy
two-state conversion, and rejects a lossy Logic9-to-aval/bval request. The
generated runtime ABI preserves its original v1 prefix and appends
pointer-based four-plane Logic9 callbacks plus caller-owned third/fourth
register planes. Per-process/per-signal SimIR value-kind metadata selects the
correct path and drives explicit conversion at mixed-domain boundaries.

Concrete VHDL user-array layout retains a source-ordered dimension vector in
addition to the packed runtime view. Each dimension stores its exact evaluated
left/right bounds, `to`/`downto` direction, null state, and packed-bit stride;
the rightmost dimension varies fastest. Layout begins with the complete scalar,
vector, record, enumeration, or nested-array element width and multiplies
dimension extents from right to left with checked 64-bit arithmetic. A concrete
null dimension produces total width zero without discarding its source range,
while an unconstrained dimension leaves total width absent. One-dimensional
arrays retain their declared packed-range mirror for existing execution;
multidimensional arrays receive a normalized total packed range. Nominal array
and element identities remain independent of this flattening and participate in
generic-specialization identity.
Contextual VHDL array aggregates consume that dimension vector recursively.
At each level, the declared right bound maps to packed offset zero and each
index advances by the dimension stride; positional associations begin at the
declared left bound. Named scalar, directed-range, choice-list, and final
`others` associations mark logical elements rather than individual bits, so
coverage and overlap remain correct for composite elements. A nested aggregate
receives either the remaining multidimensional view or the retained element
type, allowing the same lowerer to assemble nested arrays and nominal records
into common fixed-width `Insert` operations.
Nested record layout uses the same retained type graph recursively. Each
member carries its concrete nested type, nominal identity, state domain, and
packed offset; records and arrays therefore compose without flattening away
the semantic profile. Same-language boundary compatibility walks that graph,
and `vhdl-array-shape-v2` serializes it recursively into adapted-port
specialization identities. Read and assignment selection chains likewise
alternate member and array steps while accumulating exact packed offsets and
runtime bounds checks. Driver-region and sensitivity analysis consume the same
chain, so disjoint nested targets remain independently owned while debugger
and VCD views retain the deterministic whole-object packed representation.

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
units. SystemVerilog compilation-unit and module-local
`timeunit`/`timeprecision` declarations override inherited directive context.
Decimal/scientific delays retain an exact bounded rational representation,
optional explicit physical-unit suffixes override the module unit, and
conversion rounds to the declared precision before converting exactly to
global ticks. Half steps round upward. Parenthesized `min:typ:max` delay
triples retain all branches; schema-1 `[run].delay_mode` or `--delay-mode`
selects `min`, `typ`, or `max` before precision rounding and automatic global
resolution, with `typ` as the deterministic default. The selected mode is part
of whole-design and specialization native-cache identity. Parenthesized
continuous-assignment delays retain up to three independently selectable
values for rise, fall, and turnoff; the supported gate forms retain up to two.
One value applies to all transitions and an omitted turnoff delay is the
minimum selected rise/fall value. Every list member participates in automatic
resolution selection. Inexact, overflowing, malformed, late/duplicate, or
coarser-than-unit declarations are rejected. Parameterized/nonconstant delay
expressions and SystemC participation in automatic resolution selection are
not complete.

The same ordered token stream carries Verilog compiler state across shared
roots. `` `default_nettype`` selects scalar implicit-net and untyped-port net
types, with `none` producing a source diagnostic. `` `celldefine`` marks each
following module and that bit is retained on its elaborated specialization.
`` `unconnected_drive`` is captured on each instance and materializes an
omitted input as a `pull0`/`pull1` initialized signal. `` `resetall`` restores
time, net, cell, and unconnected-drive defaults. `` `begin_keywords`` scopes
the IEEE 1364/1800 reserved-identifier set until its matching
`` `end_keywords``. Active `` `line`` directives remap the following token
stream's logical file/line coordinates, built-in macros, expansion ancestry,
diagnostics, debug points, and report callbacks. The span separately retains
the physical input identity for library ownership and analysis/native cache
provenance; mapping state is local to each included file or compilation-unit
root. The current runtime combines these net forms through common wired and
strength-aware four-state driver resolution, including implicit pulls,
supplies, transmission networks, and `trireg` charge storage. Standardized
pragma payload behavior remains incomplete.

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
Immediate Verilog/SystemVerilog named-event triggers use a zero-initialized
internal event bit and toggle it with an active-phase blocking write. Existing
any-change sensitivity fanout therefore wakes static or dynamically suspended
event waiters in the next deterministic delta. SystemVerilog nonblocking
`->>` uses the same toggle value but publishes it with `WriteUpdate` in the
common update phase; an attached integer delay selects `WriteAfter` and a
future timestamp. Affected waiters resume in the following delta.

The four queues are the implementation spine for the more detailed
cross-language lattice in
[cross-language-semantics.md](cross-language-semantics.md). Language-specific
driver transactions, SystemC channel updates, net resolution, and all postponed
callbacks are not yet complete.

### Verilog specify timing

Specify blocks remain module-owned frontend HIR until parameter specialization
has selected every scalar or mintypmax `specparam`. Hierarchy elaboration then
resolves their terminals through the same generated-instance signal and port
alias map used by ordinary drivers. It emits immutable module-path arcs and
timing-check records containing dense signal/lane identities, normalized
project ticks, bounded expression programs, source provenance, and exact
transition, edge, polarity, pulse, and optional-argument metadata.

The scheduler evaluates ordered `if` paths before `ifnone`, routes selected
transitions through its cancelable inertial event handles, and records pending
pulse state for onevent/ondetect and showcancelled behavior. Timing checks keep
per-instance event history, compound deadlines, delayed reference/data copies,
and notifier state. Violations use the ordinary report callback, while any
notifier toggle and path output use normal signal publication so debugger,
callbacks, VCD, force/release, and resolved-driver behavior remain shared with
untimed logic.

Owning-unit, portable-library, and runtime-state schemas serialize this HIR and
normalized state with checked enums, dense IDs, expression roots, sizes, and
time ranges. The selected specify specialization and normalized records enter
native-cache provenance; restored state is validated before either interpreter
or LLVM execution. SDF annotation is deliberately outside this layer and will
translate into the same normalized timing model in its dedicated v2 batch.

## SimIR

SimIR processes are explicit state machines. The current operation set includes:

- constant loads, signal reads, delta-scoped signal-event queries,
  transaction-activity queries, previous-effective-value reads, and
  elapsed-since-event queries;
- unary/logical/reduction operations plus typed bitwise, fixed-width
  arithmetic, shift, conditional-select, and comparison operations;
- checked signed 32-bit VHDL integer unary/binary operations plus explicit
  elaborated subtype-range checks, with typed unknown, overflow,
  negative-exponent, divide-by-zero, and range failures;
- whole and normalized partial blocking writes, update-phase and transport
  delayed writes, whole/slice transition-aware inertial writes, and
  whole/slice VHDL projected-waveform writes;
- timed, dynamic-signal, static-sensitivity, and combined event-or-timeout
  waits;
- next-delta yields;
- jumps and branches;
- assertions and synchronous already-formatted language output; and
- process halt and simulation stop.

Bounded frontend lowering reaches these suspension operations from VHDL bare,
`on`, `until`, and `for` wait clauses, SystemVerilog integer `#` delay and
any-change/`posedge`/`negedge` `@(signal-list)` statements, and static process
sensitivities. Immediate named-event triggers lower to read/not/blocking-write
operations while `@event` uses the same any-change wait. Dynamic and static
edge waits share the same four-state edge
predicate. VHDL condition waits suspend before their first condition test.
Combined event/timeout waits retain one absolute deadline while false event
wakeups rearm the sensitivity set; an internal scalar frame register records
whether the eventual wake was the timeout. A VHDL process containing explicit
waits jumps back to its post-initializer entry when its body completes,
preserving implicit process repetition without reinitializing locals.

Delayed Verilog/SystemVerilog continuous assignments lower to the inertial
write forms with normalized rise/fall/turnoff ticks. At scheduling time, the
kernel compares the new packed value with the currently driven target:
transitions to `1`, `0`, and `Z` select rise, fall, and turnoff respectively,
while transitions to `X` select the shortest delay. A packed write uses the
shortest delay required by any changed element. Each process/target/slice has
an independent cancelable scheduler handle so a later evaluation removes its
pending transaction and timestamp, including a short pulse that returns to
the current driven value. Procedural delayed nonblocking assignments retain
`WriteAfter` transport behavior.

Combinational UDP rows lower to an ordered chain of ordinary four-state
case-equality conditions; unmatched inputs drive `X`. Sequential rows add
per-instance previous-input, current-output, and optional initialization state
to one sole-driver process, with edge descriptors lowered through the same
packed operations. The table process drives a hidden scalar `$udp_value` and
an ordinary continuous assignment drives the public output. UDP propagation
delays therefore reuse the common rise/fall/turnoff inertial scheduler,
cancellation, callbacks, debugger visibility, VCD publication, LLVM lowering,
and cache validation without a UDP-specific runtime operation or ABI callback.

Verilog/SystemVerilog intra-assignment controls retain a distinct typed HIR
kind. For a blocking `target = #delay expression`, lowering evaluates the
expression into the persistent process frame before a debugger-visible
`WaitFor`, then publishes the retained value in the resumed active phase. A
nonblocking `target <= #delay expression` instead evaluates immediately,
schedules `WriteAfter`/`WriteAfterSlice` into the destination timestamp's
common update phase, and continues without suspending. An intra-assignment
event control emits `WaitOn` first, so both blocking and nonblocking forms
evaluate the RHS only after the selected any-change or scalar edge event.
Wildcard assignment controls infer their dynamic sensitivity from readable
RHS signals.

Update staging is append ordered. Initial processes run in stable process-ID
order, and same-timestamp delayed callbacks use that same stable order.
Whole and slice updates are then folded from the current driven value in
staging order, making the last overlapping assignment win while committing
only one effective value per signal. This rule applies uniformly to multiple
assignments in one process, assignments from different processes, `#0`
delayed NBAs, and equal future deadlines.

VHDL signal assignments lower to `WriteProjected` or
`WriteProjectedSlice`. The kernel owns an ordered cancelable transaction list
for every process, signal, and scalar target subelement. Transport deletes
transactions at or after the first new transaction and appends the new
transaction. Inertial mode then applies the projected-output marking algorithm
using the explicit rejection limit, or the first waveform delay by default:
new transactions, sufficiently old transactions, same-valued predecessors,
and the current transaction are retained while other old transactions are
cancelled. Packed writes apply this independently to each scalar and reconstruct
the committed vector through update-phase slice coalescing.

The initial output slices lower literal or empty Verilog/SystemVerilog
`$display`, `$write`, and `$strobe` calls to a typed `Display` operation
carrying explicit newline and immediate/postponed policy. The interpreter
invokes an
embedding-owned output hook synchronously with process, time, and delta
metadata. Compiled O0/O2 code calls the same hook through an append-only
plain-C runtime-table tail, so output ordering remains part of the common
single-thread simulation semantics. `$strobe` publication is scheduled into
the current timestamp's postponed phase through a second append-only callback.
Multiple conversion/value pairs lower in source order. Additional operands
use typed default-decimal formatting, while `%m` is folded from the elaborated
scope and `%t` reads the current global simulation tick.
Output-task literal spelling is decoded once in the frontend for newline, tab,
quote, backslash, and one-byte octal escapes; SimIR and generated code retain
the exact byte string, including embedded NUL bytes.
The same decoder is used for bounded `$fatal` and immediate-assertion `$error`
literal messages before assertion metadata enters SimIR.
VHDL assertions and `report` statements retain general string report and
`severity_level` expressions. Literal/predefined forms keep the compact typed
`Assert` and `Report` operations; dynamic forms lower string concatenation and
the two-bit severity ordinal into registers consumed by `StringReport`.
Passing assertions branch over both expression evaluations. `note`, `warning`,
and `error` invoke a synchronous report hook and continue in both engines.
The LLVM adapter uses the same instruction-index callback to read dynamic
registers without extending the C ABI. A standalone `failure` publishes once
then terminates, while a failed assertion terminates without double-reporting.
Both preserve exact source metadata. VHDL doubled quotes are decoded in the
frontend; a configurable stop threshold remains targeted.
VHDL file objects reuse the common manifest-confined file service while
keeping their source-level state in opaque 32-bit registers. `FileOpen`
distinguishes status and nonstatus calls, returns the four predefined status
ordinals without clearing an already-open object, and never exposes a host
descriptor. VHDL `FileClose` clears the owning register; epilogue cleanup can
therefore close lexical and procedure-owned objects idempotently, including
after an early return. Lookahead `FileEndOfFile` reads and restores one byte.
Direct integer-element `read` uses `FileScan` with required-assignment metadata
so both engines reject incomplete conversion, while direct `write` shares the
signed-decimal formatter. File-formal call frames copy the opaque state in and
back out, preserving alias behavior without transferring host ownership.
Bounded TextIO maps `line` to the existing byte-string register plane.
`FileReadLine` marks TextIO reads so the common service strips LF/CRLF and
rejects true EOF; `FileWriteString` can clear its source after the newline.
String-backed `FileScan` reports consumed bytes and optionally stores a
one-bit success result, allowing `read(line, value, good)` to advance the same
cursor without mutating a failed target. Required reads share the same scan
but turn incomplete conversion into an exact runtime failure. TextIO writes
append through `StringMethod` formatting, so interpreter and compiled paths
share width, justification, byte-limit, and Boolean spelling behavior. Schema
73 keys every added TextIO/file operation field.
VHDL's predefined `time` is a nonnegative signed-64-bit tick value. Before
elaboration, the application recursively rewrites standard physical units
from `fs` through `hr` into exact project ticks, including units nested in
locally static constants, qualifications, arithmetic, comparisons, and
expression-valued waits. Rational cancellation avoids an intermediate
femtosecond overflow, while nonstatic, final-tick overflow, and inexact
resolution cases receive distinct diagnostics. Expression-valued units also
participate in automatic resolution selection. The resulting integer SimIR
wait/timeout representation is shared unchanged by the interpreter and LLVM;
native schema 75 separates these and the representation-derived container
storage semantics from prior cached objects.
The Batch 119 integration boundary keeps those operations inside ordinary
specialization ownership. A time-generic VHDL child produces a distinct
canonical native module beside its parent; both modules retain the same keys
across reference, O0/O2, warm-cache, and forced-O0 debug construction.
Execution-point metadata carries the child lexical scope and original VHDL
source through nested callable suspension. Reports and file/TextIO operations
likewise retain source points across cold/warm compiled runs, while VCD
observes only committed parent-visible values and resolved projected
transactions.
Literal-only `$monitor` retains its one initial postponed publication.
Value-sensitive `$monitor` installs one runtime-owned global registration.
The current direct packed-signal slice publishes once after installation,
coalesces watched committed changes within a time/delta slot, and renders
their final values in the postponed phase. A later registration replaces the
earlier one. `$monitoroff` suppresses pending and subsequent publications
without discarding the registration; `$monitoron` re-enables it and schedules
one current-value publication. Interpreter and LLVM operations call the same
registration through append-only plain-C callbacks.
Known unsigned numeric literals used as the sole output-task argument are
normalized to their width-truncated decimal value in typed HIR. Unknown-state,
dynamic and additional operands use runtime formatting. Based literals marked
signed are interpreted as two's-complement at their declared width before
decimal formatting.
The dynamic formatting spine lowers `$display`/`$write` `%b`, `%h`/`%x`,
`%o`, `%d`, `%c`, or `%s` conversions to ordered `FormatDisplay` operations,
which retain typed source registers,
prefix/suffix text, newline policy, and conversion kind. The interpreter
formats the full packed value through the common four-state kernel. LLVM code
passes its evaluated word plus immutable instruction identity through an
append-only callback and therefore uses the same formatter and embedding
output hook. Uppercase spellings normalize to the same lowercase-output
policy, and `%%` is collapsed in the frontend. Decimal minimum widths,
left justification, and numeric zero padding are typed metadata; negative
decimal zero padding follows the sign. `%m` emits the elaborated scope without
consuming a value. `TimeDisplay` captures `%t` from the current global tick
and shares the field-padding policy in interpreter and compiled execution.
Hex formatting retains `ceil(width/4)` digits. Uniform X/Z nibbles remain
`x`/`z`; a nibble mixing known and unknown states conservatively renders `x`.
Octal applies the same policy to `ceil(width/3)` three-bit groups.
Character formatting consumes the least-significant eight bits and emits one
byte; an X/Z in those bits renders the deterministic text `x`.
Packed-string formatting emits bytes most-significant first, omits leading
zero-padding bytes, and renders each X/Z-containing byte as `x`.
The `%0b`, `%0h`, and `%0o` forms retain a typed suppression flag and remove
only leading known-zero digits, always leaving at least one digit.
Decimal formatting uses an arbitrary-width binary-to-decimal kernel, derives
two's-complement interpretation from the typed expression, and renders any
four-state unknown value as `x`.
`FormatDisplay` also carries postponed policy. For `$strobe`, the common
runtime formats and owns the complete text when the operation executes, then
schedules that immutable text in the timestamp's postponed worklist. Later
active/update changes therefore cannot alter the captured result.

For bounded `always @*`, `always_comb`, `always_latch`, and dynamic `@*`,
elaboration walks executable statement expressions, excludes assignment
targets, and converts the sorted set of readable signals into any-change
sensitivities. `always_comb` and `always_latch` enter their bodies once at time
zero before using the inferred static list; `always @*` waits for the first
change, while dynamic `@*` installs an inferred `WaitOn`. Function/task body
dependencies remain pending.

Verilog/SystemVerilog `case`, `casez`, and `casex` evaluate their selector
once, test comma-separated choices in source order with dedicated four-state
matching operations, and execute `default` only when no choice matches.
Exact `case` treats `X` and `Z` as values. `casez` treats `Z` (including the
binary `?` spelling) in either the selector or choice as a wildcard, while
`casex` treats both `X` and `Z` on either side as wildcards. Every comparison
produces a two-state condition, so interpreter and LLVM branch behavior is
identical. Bounded `case inside` adds right-choice wildcard values and closed
ranges; bounded `case matches` adds exact constant patterns and `.*`.
`unique`/`unique0`/`priority` compute alternative-level match counts before
retaining the ordinary first-body/default selection.

Sequential conditional statements lower recursively to explicit SimIR
branches and exit jumps, preserving source order and the nearest-`else`
association. VHDL `if` and `elsif` conditions must lower to the Boolean domain;
Boolean literals and Boolean logical/equality operations retain that domain.
SystemVerilog conditions first reduce the complete packed expression to
four-state truth: any known `1` is true, an all-zero value is false, and a
value containing only zero plus `X`/`Z` is indeterminate. The procedural
branch policy treats that indeterminate result as false. Immediate
SystemVerilog assertions use the same normalization but retain the
indeterminate scalar so the assertion fails. Both interpreter and LLVM paths
execute the resulting common logical and branch operations.

Bounded SystemVerilog conditional expressions use three explicit control-flow
paths after packed vector truth conversion. A definitely true or false
condition evaluates only its selected, context-sized alternative; an X/Z
condition evaluates each alternative once and feeds their converted values to
the common typed SimIR select, which preserves identical four-state bits and
produces `X` where they differ. VHDL-2008 conditional assignments retain their
Boolean-only typed-select path. Chained VHDL `when`/`else` alternatives nest
from left to right so the first true condition wins. Both interpreters and LLVM
execute the same branch and select graph.

Every successfully lowered scalar expression appends a source location,
resolved width, signedness, self/context sizing kind, and value-domain profile
to immutable SimIR. These profiles make the supported SystemVerilog conversion
decisions reviewable after elaboration and participate in schema-53 native
cache identity. JIT validation rejects zero widths and unknown sizing/domain
enum values before code generation; a wider reference-only process retains its
exact profile while the existing register-width check selects fallback rather
than converting the profile into a hard error.

VHDL selected concurrent assignments normalize to one exact-case process.
Each source alternative owns a normal continuous assignment, so whole and
constant-selected targets, update-phase writes, and optional single-waveform
delays plus the selected assignment's common inertial/transport/reject
mechanism reuse the ordinary assignment path. The generated process is sensitive
to the selector and every alternative value dependency; choice expressions
also participate defensively, although the supported source form expects
locally static exact choices. A final `others` is required by the bounded form
to guarantee that every evaluation schedules exactly one alternative.

VHDL procedure calls use the ordinary resumable SimIR call stack, so a wait
inside a called procedure suspends the owning process without discarding its
return frame. Elaboration records exact overload-resolved procedure edges as
call sites are lowered. After all reachable callable bodies are present, the
transitive suspension closure selects a no-sensitivity process's implicit
repeat jump and rejects the same closure from a sensitized process or a
function. Deferring that decision avoids both name-only overload false
positives and fallthrough from the process body into appended subroutines.

SystemVerilog final procedures lower to ordinary resumable SimIR processes
marked `final` and `initialize = false`. When ordinary scheduling becomes
quiescent, or a SimIR `Stop` records a design `$finish`, the interpreter queues
all final processes once in stable process-ID order at the current timestamp.
For `$finish`, the scheduler first discards ordinary current, next-delta, and
future work; only final processes are then queued, so a pending timed `forever`
process cannot resume past the terminal stop.
Their blocking writes and update phase complete before the run result is
returned. An external debugger stop does not trigger finals, and a design
stop remains distinguishable after finals complete. The frontend rejects
timing controls, waits, `$finish`, and NBAs in a final body, preventing a final
procedure from suspending or scheduling future time.

Verilog/SystemVerilog `$stop` lowers to a distinct SimIR `Pause` boundary.
Both engines preserve the process frame at the following instruction and
return a non-design stop to the application. Clearing the scheduler stop and
running again resumes that process without replaying earlier statements.
Unlike terminal `$finish`, a pause does not execute final procedures; finals
run only after the resumed design reaches quiescence or `$finish`. LLVM exposes
this boundary through append-only native resume status `10`, while leaving the
frame in the ready state.

SystemVerilog logical negation reduces a packed operand using four-state truth
semantics: any known `1` makes `!` false, an otherwise unknown-containing
operand produces `X`, and an all-zero operand produces true. Unsigned
inequality and relational comparisons require equal operand widths and return
`X` if either operand contains `X` or `Z`; known operands compare exactly.
Case equality `===` compares both packed value and unknown-state planes, so
matching `X` and matching `Z` are equal while `X` and `Z` differ; it always
returns a known scalar. Case inequality `!==` applies a known scalar inversion
to that result. SystemVerilog wildcard equality `==?` masks `X` and `Z` bits
only in its right operand. An unmasked left-side `X` or `Z` produces `X`;
otherwise the remaining known bits determine equality. Wildcard inequality
`!=?` applies four-state inversion, preserving an indeterminate result.
The interpreter supports arbitrary packed widths while LLVM uses the common
single-word fast path and falls back for wider value-bearing processes.

Logical conjunction and disjunction reduce the left operand first, so packed
operands need not have the same width. A known false controls `&&` and a known
true controls `||`, branching past the complete right-hand graph. Otherwise
the right operand executes once and the common four-state logical operation
produces `X` only when neither value controls the result. Time-free bounded
function writes to nonlocal variables provide executable side-effect evidence
that skipped calls are not evaluated.

Unary reduction `&`, `|`, and `^` fold every packed source bit into one
four-state result. The complemented SystemVerilog forms `~&`, `~|`, `~^`, and
`^~` apply four-state inversion to that scalar result. Binary `~^` and `^~`
likewise lower to bitwise XOR followed by four-state inversion, keeping both
spellings on the same SimIR path. Logical `<<` and `>>` preserve the left
operand's width and four-state data independently of the shift-amount width. A
known amount at least as large as the value width produces zero; any `X` or
`Z` bit in the amount produces an all-`X` result. These rules are implemented
identically in the interpreter and the LLVM single-word path.

Fixed-width unsigned arithmetic supports addition, subtraction,
multiplication, division, and remainder, with overflow truncated to the
operand width. Any `X` or `Z` operand bit makes the complete arithmetic result
unknown. Division and remainder by zero likewise produce an all-`X` result;
the LLVM path selects a safe internal divisor before its native operation, so
the generated code cannot execute LLVM's undefined integer divide-by-zero
case. Unary plus preserves its operand and unary minus lowers to width-matched
zero minus the operand.

Signed packed arithmetic uses two's-complement values at the same fixed width.
Addition, subtraction, and multiplication retain their signed SimIR type while
sharing the modulo-\(2^N\) bit result. Signed division truncates toward zero;
remainder has the dividend's sign, while VHDL `mod` adjusts a nonzero remainder
to the divisor's sign. The minimum value divided by negative one wraps to the
minimum bit pattern without exposing LLVM signed-division overflow. Signed
comparisons sign-extend the declared width. Any unknown operand bit and every
zero divisor produce the same all-`X` result in the arbitrary-width interpreter
and LLVM single-word path. SystemVerilog chooses signed arithmetic only when
both operands are signed; an unsigned operand makes the operation unsigned.
The bounded VHDL path rejects explicitly mixed signed/unsigned operands while
allowing an integer literal to take its surrounding numeric context.
Packed exponentiation uses exponentiation by squaring in the arbitrary-width
interpreter and a fixed unrolled bit scan in eligible LLVM modules. Positive
exponents wrap modulo the destination width, zero exponents produce one, and
any `X`/`Z` operand produces all `X`. SystemVerilog `**` associates
left-to-right; signed negative exponents produce zero except for bases `1` and
`-1`, while zero raised to a negative exponent produces all `X`. The bounded
VHDL numeric-vector form follows VHDL factor syntax: an unparenthesized factor
contains at most one `**`, a leading sign applies outside that factor, and the
exponent must be a locally static nonnegative integer. Integral constant
folding uses checked signed 64-bit exponentiation and diagnoses overflow.
SystemVerilog procedural compound assignments normalize in typed HIR to a
blocking assignment whose value is the corresponding binary operation over a
read of the target. Standalone prefix and postfix increment/decrement use the
same representation with a contextual unit operand; because their values are
not consumed as expressions in this bounded form, both placements have the
same read-modify-write behavior. Constant bit, part, indexed-part, and packed
member targets reuse the normal selected-read and selected-write lowering.
Packed VHDL `abs` accepts a signed operand. It extracts the leftmost sign
element, computes the same-width two's-complement negation, and selects the
original or negated value through the common four-state conditional operation.
The minimum negative value therefore wraps at its declared width; a negative
operand containing any `X` or `Z` produces the arithmetic all-unknown result.

SystemVerilog `$signed` and `$unsigned` are bounded packed type casts in the
current executable path. They preserve the operand's bits, width, and state
domain while changing the signedness used by enclosing arithmetic shifts,
comparisons, and arithmetic operations. Constant folding treats either cast as
a value-preserving operation; incorrect arity is diagnosed before lowering.
SystemVerilog `$isunknown` compares an operand with itself using ordinary
four-state equality, then case-compares that scalar result with `X`. The result
is therefore a known one exactly when any operand bit is `X` or `Z`, and known
zero otherwise, using operations shared by the interpreter and LLVM paths.
SystemVerilog `$bits` asks the typed expression-width inference layer for its
packed argument's complete static width and materializes that number as a
known 32-bit two-state value. The operand is not evaluated. Type arguments,
unpacked objects, and dynamically sized objects remain outside this bounded
form.
The bounded one-dimensional `$left`, `$right`, `$low`, `$high`, `$size`, and
`$increment` queries additionally read the declared packed range retained in
DesignIR. They preserve ascending versus descending source bounds and
materialize a known signed 32-bit result; `$increment` returns `1` for a
descending range and `-1` for an ascending range. The optional dimension
argument is accepted when it is the locally static value `1`.
`$dimensions` and `$unpacked_dimensions` use the same retained metadata and
return `1` and `0`, respectively, for the currently supported packed-only
objects. Direct supported one-dimensional integral unpacked-container objects
share this query surface. Static arrays fold their specialized bounds,
direction, size, element-width bit count, and `2`/`1` total/unpacked dimension
counts without reading storage. Dynamic arrays and queues compose the existing
`ContainerSize` operation with signed subtraction or unsigned multiplication;
empty right/high is therefore `-1`. Associative arrays expose entry-count
`$size`, entry-width `$bits`, and `2`/`1` dimension counts while finite-bound
queries diagnose. Type-only, indirect, and multidimensional container queries
remain pending.

SystemVerilog apostrophe-brace assignment patterns are retained as aggregate
HIR with positional, keyed, or source-spanned `DefaultChoice` association
metadata, distinct from packed concatenation. A direct whole-container
assignment allocates a temporary register of the exact specialized target
type. Static positional members write declared indices in left-to-right
order. A bounded keyed/default static pattern evaluates member values in
source order, converts each key to the signed 32-bit declared-index profile,
fills every unmentioned ascending or descending index from its one default,
then applies explicit keys. Dynamic arrays resize before indexed writes;
queues append through `PushContainer`; associative members convert and
deduplicate locally constant keys before `ContainerWrite`. Only after the
temporary is complete does `CopyContainerRegister` replace the local or
module-object value. No new SimIR operation, native callback, allocator
identity, or address-bearing ABI is required. Schema 38 distinguishes the
expanded construction semantics while canonical operations retain member
order, values, keys, destination profile, and source provenance.

SystemVerilog direct unpacked-container `sum`, `product`, `and`, `or`, and
`xor` methods lower to a typed `ContainerReduction` operation whose scalar
result preserves the exact element width, state domain, and signedness.
Container storage is already canonical: static arrays use declared order,
dynamic arrays and queues use current index order, and associative elements
are paired with sorted keys. The shared reduction kernel therefore scans that
storage directly. Empty values start from the exact-width language identity;
arithmetic uses the common unknown-propagating fixed-width kernel and bitwise
methods use the common per-bit four-state truth tables. Native execution routes
the operation through the existing generic container callback, so no ABI slot
or address-bearing representation is added.

SystemVerilog direct writable unpacked-container `reverse`, `sort`, and
`rsort` method statements lower to `OrderContainer`. The operation retains
only its enum and typed container register; object and port receivers reuse
the existing explicit writeback. Static arrays expose declared left-to-right
storage and dynamic arrays and queues expose current index order, so reversal
and stable sorting mutate that sequence without remapping indices or changing
capacity. The exact-width comparator scans most-significant bit first. Its
ordinary rank is `0 < 1 < X < Z`; the signed sign-bit rank is
`1 < 0 < X < Z`, which preserves two's-complement numeric order for known
values and gives unknown sign states deterministic positions. The interpreter
and native generic container callback invoke the same kernel, and schema 31
serializes the operator plus this semantic policy without extending the
append-only C ABI.

SystemVerilog direct nonassociative unpacked-container `min`, `max`, `unique`,
and `unique_index` expressions lower contextually to `LocateContainer` when
assigned to a compatible queue. Value-result queues retain the receiver's
exact element profile; index-result queues use signed two-state 32-bit
elements. Extrema reuse the ordering comparator and retain the first equal
candidate. Uniqueness scans declared/current storage order, compares complete
four-state values, and emits either the first value or its signed declared or
current index. The kernel copies source elements before replacing the result,
so an aliased queue expression is deterministic. Interpreter and native paths
again use the generic container callback; schema 32 serializes the locator,
source, destination, result type, and shared comparison policy without adding
a public ABI slot. LLVM validation support is split from the main validator
so both authored compilation units remain below the hard source-size limit.

`$onehot` and `$onehot0` lower to dedicated common reduction operators. They
count exact `1` elements of the packed operand, ignore `X` and `Z` elements,
and return a two-state bit indicating exactly one or at most one set element.
The interpreter kernel scans arbitrary widths; LLVM emits an allocation-free
single-word reduction for eligible compiled processes.
`$countones` uses a separate common operation with a signed 32-bit result. The
interpreter scans the packed value once without allocating intermediate
registers. LLVM counts exact known-one bits from the `aval`/`bval` planes for
eligible single-word processes; `X` and `Z` never contribute to the count.
`$countbits` carries a four-bit `0`/`1`/`X`/`Z` selection mask in its common
operation and native-cache identity. The interpreter compares each packed
element with that exact-state mask. LLVM derives each selected state directly
from the two value planes and accumulates a known signed 32-bit result.

A VHDL concurrent assertion becomes a common implicit process. Its condition
dependencies form the static sensitivity set, its optional label becomes the
stable process name, and the existing assertion operation retains message,
severity, and source metadata. The process executes once at initialization and
again after each matching signal change.

VHDL `sll` and `srl` use the common zero-filling packed shift operations.
`sla` replicates the rightmost packed element and `sra` replicates the
leftmost packed element, including `X` or `Z`. `rol` and `ror` reduce the
count modulo the packed width and retain every four-state element. Locally
static and dynamic base-`integer` negative counts reverse the operation
direction (`sll`/`srl`, `sla`/`sra`, or `rol`/`ror`) before using the
absolute magnitude. SimIR records signed-count intent separately from the
packed amount, so the arbitrary-width interpreter and allocation-free LLVM
path share the same rule and native-object identity.

The bounded VHDL base `integer` subtype is represented as a signed 32-bit
two-state object for ports, signals, and process variables. Its default value
is the left bound `-2^31`; ordinary in-range unary, arithmetic, `abs`,
`rem`/`mod`, relational, assignment, debug, VCD, and compatible hierarchy
paths reuse the common packed runtime. Checked overflow, explicit scalar
ranges, and runtime `natural`/`positive` subtype enforcement remain pending,
so this is not yet complete VHDL integer semantics.

DesignIR retains each signal's optional declared packed range in addition to
its normalized storage width. Constant SystemVerilog bit/part selects and VHDL
indexed names/slices map source indices to storage offsets by distance from
the declaration's right bound, so descending, ascending, and non-zero-based
ranges preserve their source ordering. Slice direction must match the
declaration in the current bounded form. SimIR `Extract` operates on
normalized offsets, while `Concatenate` places ordered source operands from
most to least significant. SystemVerilog brace concatenation and VHDL `&`
share that operation; the interpreter supports arbitrary widths and LLVM
lowers the single-word case.

Constant selected assignment targets use the same declared-range mapping.
SimIR `Insert` handles packed procedural-local updates. Signal targets lower to
typed partial blocking, common-update, or delayed writes. The kernel merges
the ordered whole/partial update sequence against the driven value during the
common update phase, so overlapping assignments retain stable process/source
order while an intervening active-phase blocking write remains visible.
Delayed partial writes enter that same sequence at their due timestamp rather
than capturing unrelated bits when the assignment is issued. The appended
plain-C runtime callbacks carry only normalized offset, width, and one-word
`aval`/`bval` data.

Dynamic single-element selections retain the elaborated left/right bounds
and base offset in SimIR together with a signed 32-bit index register.
Interpreter and LLVM lowering first reject unknown indices, check the source
index against the declared inclusive range, and then normalize it by distance
from the declaration's right bound. `DynamicExtract` and `DynamicInsert`
preserve Logic4/Logic9 planes directly. Dynamic signal writes resolve and
capture the normalized offset when the assignment executes, then reuse the
existing static-slice runtime callbacks and driver scheduling; this keeps the
plain-C callback ABI unchanged while giving delayed, inertial, NBA/update,
projected, and projected-waveform writes identical target-index semantics.

Fixed-width dynamic VHDL slices extend that representation with a contextual
1-through-64-bit selection width and two signed 32-bit integer-family bounds.
Elaboration checks both bounds against the declared packed range, preserves
ascending or descending direction, and requires their runtime distance to
match the contextual width exactly. The right bound anchors the normalized
offset, so reads and local writes reuse dynamic extract/insert while signal
writes reuse the dynamic update, delayed, inertial, projected, and atomic
projected-waveform paths. Target bounds are captured when the assignment
executes; concurrent target-bound expressions contribute sensitivity without
making the written signal self-sensitive. Runtime-sized results and another
selection after a dynamic slice remain outside this bounded contract.

The deterministic simulation kernel owns process PCs and boundary scheduling.
Reference processes use interpreter-owned register frames; compiled processes
use caller-owned LLVM frames through the `ProcessExecutor` boundary. Both paths
report waits, yields, stop, and halt through the same kernel boundary handler.
For `WaitOn`, `WaitSensitivity`, and `WaitForever`, the boundary reports only
the instruction index. The immutable SimIR process continues to own the
ordered dynamic signal list and the static signal/edge rules, so generated
code does not copy scheduler metadata across the ABI. The kernel validates the
returned instruction and sequential resume PC, then installs or observes the
corresponding sensitivity or permanent suspension.
Every simulation test added for a compiled operation should run through both
paths and compare output, final state, assertions, and trace events.

## LLVM boundary and native cache

Supported compiled builds use LLVM 22.1.8, ORC, and LLJIT. The adapter public
header exposes no LLVM class. Generated functions receive a versioned C table
containing opaque context plus signal-read, blocking-write, assertion,
update-write, delayed-write, transition-aware whole/slice inertial-write, and
projected whole/slice single-write and atomic waveform-array callbacks plus
signal-event callbacks. The `write_update`,
`write_after`, `signal_event`,
`signal_last_value`, `signal_last_event`, `signal_active`, `write_inertial`,
`write_inertial_slice`, `write_projected`, and `write_projected_slice`
callbacks, followed by `write_projected_waveform` and
`write_projected_waveform_slice`, are append-only extensions of the v1
table: original field offsets remain fixed, and each compiled process checks
`struct_size` only for the callback tail it actually uses. A process using
only an earlier operation set therefore remains valid with the corresponding
v1 prefix.
`SignalEvent` and `SignalLastEvent` consult the kernel-owned value-change
stamp, `SignalActive` consults the separate transaction stamp, and
`SignalLastValue` reads the previous effective packed value retained at commit;
none copies scheduler-owned state into generated code. CMake requires
the exact supported LLVM package when `FSIM_LLVM_MODE=ON`; the checked-in Linux
LLVM job builds and runs the adapter suite against 22.1.8. A separate C11 test
verifies the offsets, extended size, callback handoff, and genuine C ABI.
The Batch 139 append-only tail adds `signal_last_active`, `signal_driving`,
`signal_driving_value`, and `signal_driving_value_logic9` at offsets 512 through
536, preserving a 544-byte compatible prefix. Batch 141 appends
`read_simulation_time` and `vital_timing_check` at offsets 544 and 552 for a
560-byte table. Batch 142 appends `vital_delay` at offset 560 for a 568-byte
table while retaining the complete 560-byte timing-check prefix.
Transaction-sensitive support processes drive
interned `'stable(T)`, `'quiet(T)`, `'transaction`, and `'delayed(T)` signals
through ordinary projected writes; their scheduler state therefore remains in
the serialized common signal/process graph rather than in generated code.
`WAIT_ON`, `WAIT_SENSITIVITY`, `DEBUG_POINT`, and `WAIT_FOREVER` are append-only
resume-status values 6 through 9; values 0 through 5, the v1 result ABI
version, and the 24-byte result layout are unchanged. The existing instruction
field identifies the immutable SimIR boundary operation, and delay remains
meaningful only for `WAIT_FOR`.

The current adapter compiles control-flow graphs containing loads, reads,
common operations, blocking writes, assertions, jumps, branches, timed waits,
dynamic-signal waits, static-sensitivity and permanent waits, next-delta yields,
update-phase writes, transport delayed writes, whole/slice inertial writes,
whole/slice single and atomic ordered projected-waveform writes,
signal-event, transaction-activity,
previous-value, and elapsed-event-time queries, design stop, and halt.
A versioned caller-owned plain-C frame holds the process PC plus separate
`aval`/`bval` register planes; a versioned result reports completion, assertion
failure, timed/dynamic/static/permanent wait, yield, or stop. Generated
scheduled writes hand the checked `Logic4Word` planes directly to the kernel
without allocating an intermediate wide value. The kernel, rather than
generated code, owns
update coalescing, timestamp overflow checks, sensitivity installation, edge
rules, and phase scheduling. Loops are accepted when every invocation reaches
a `WaitFor`, `WaitOn`, `WaitSensitivity`, `WaitForever`, or `Yield`
suspension; reachable zero-time cycles without one of these safe boundaries
are rejected.
Sensitivity-only signals may be wider than 64 bits because their values never
cross the native word ABI. General native value operations remain on the
1-to-64-bit compiled fast path, while validated service operations may own
arbitrary-width storage directly. In particular, binary `$fread` targets and
packed memory elements use the executor's owning `PackedLogic4` service path;
the 137-bit file fixture compiles as one LLVM process at O0 and O2. Other native
capability misses retain deterministic per-process interpreter fallback.

Validation rejects empty dynamic or static lists, invalid or zero-width signal
references, invalid edge kinds, and non-scalar positive/negative-edge signals.
It also retains the existing register/dataflow, control-flow, boundary
instruction, resume-PC, frame-state, and ABI checks.
`LlvmJitUnsupportedError` identifies capability misses that the application
hybrid engine handles with per-process interpreter fallback. Malformed SimIR,
ABI mismatches, LLVM/cache failures, and generated-runtime failures remain
fatal `LlvmJitError`s. LLVM-enabled `fsim build` and `fsim run` install
compiled executors for eligible processes; within each elaborated
specialization, those processes are lowered and optimized in one LLVM module.
An unsupported sibling is omitted without preventing eligible siblings from
compiling. Builds without LLVM remain interpreter-only. LLVM-enabled
application tests compare a bounded
SystemVerilog hierarchy through reference and hybrid execution at O0 and O2,
and the vertical SV-to-VHDL-to-SV hierarchy through reference and O2 hybrid
execution. A separate exact scheduled-write comparison runs a fully compiled
O2 process, observes its update at tick 0 and delayed commit at tick 2, and
checks that callback-contained scheduling overflow is rethrown identically by
the interpreter and hybrid engines without publishing the delayed value. An
exact positive-edge application case compiles both of its two processes and
matches initial trigger publication at tick 0, the rising edge at tick 1/delta
0, the observer update at tick 1/delta 1, and the falling edge at tick 2/delta
0. These are bounded SimIR and frontend forms, not complete HDL event-control
coverage.

The persistent cache primitive provides process-aware per-key locking,
stale-owner recovery, checksummed entries, temporary-file plus atomic
replacement, corrupt-entry rejection, and safe replacement of an existing
entry. When explicitly given a cache directory, the LLVM adapter installs this
primitive through LLVM's ObjectCache hook. One native object is cached for each
compiled specialization module. Its key covers the stable module identity and
ordered canonical process keys. Each process key covers the complete supported
SimIR process, symbol, and IDs and widths of only the signals referenced by
that process, plus cache/runtime ABI schemas, exact LLVM version, O0/O2 mode,
target triple and data layout, and the detected host CPU/features. An unrelated
elaborated signal-width change therefore reuses the module object, while a
referenced signal ID or width change invalidates it.
Scheduled-write operation kind and signal/source identity participate in this
key, as does the exact 64-bit delay for `WriteAfter`; changing a delayed write
to an update write or changing its delay cannot reuse the object.
`WriteInertial` and `WriteInertialSlice` additionally key their target slice
and exact rise, fall, and turnoff delays, so changing any transition timing
invalidates the native object.
`WriteProjected` and `WriteProjectedSlice` key the target slice, transport or
inertial mode, waveform delay, and rejection limit.
Wait identity includes `WaitOn`, `WaitSensitivity`, or `WaitForever`, the
ordered dynamic signal operands, widths, and edge kinds, plus every static
sensitivity signal, width, and edge kind.
Cached objects are parsed and checked for the expected architecture before
reuse; a rejected entry is recompiled and replaced. The frame and resume-result
ABI versions and structure sizes, including the extended runtime-table size,
participate in each process key and in frame-layout identity. Group tests at O0
and O2 verify two functions per object, warm reuse, whole-module invalidation
when one member changes, and stable frame identity for an unchanged member.
Cold, warm, corruption-recovery, SimIR/referenced-width invalidation,
scheduled-write kind/delay invalidation, three-component inertial-delay
invalidation, projected mode/delay/rejection invalidation,
wait-kind/operand invalidation, and optimization-mode
invalidation are also tested at O0 and O2.

LLVM-enabled `fsim build` and `fsim run` select this cache beneath the
configured project cache as `llvm-native`. The adapter and application expose
hit, miss, store, rejected-entry, and load/store-failure counters; `fsim build`
reports the principal counters. Application tests require a cold miss and
store for every compiled specialization module followed by a warm hit with no
misses or cache failures at both O0 and O2. The two-process static-sensitivity
application fixture specifically requires one module miss/store followed by
one warm module hit. The application snapshots every HDL root and each
Verilog/SV transitive include before parsing, hashes the exact in-memory bytes
actually consumed, and retains those ordered digests with the checked root.
Repeated inclusion within one compilation unit reuses the first snapshot.
`compilation_unit = "file"` creates one state/digest per file,
`"source-set"` shares ordered macro/conditional/directive state within that
source set, and `"combined"` shares compatible language/standard state across
all source sets selecting that mode. Combined roots retain their source-set
library ownership; their include roots and manifest definitions are appended
in source-set order before preprocessing. Each specialization provenance key
covers its ordered compilation-unit roots and complete include closure,
language and standard, library, compilation-unit mode, macro/include settings,
the bundled-standard-library version marker, and
represented generic/parameter name/value pairs. A unit defined in an included
file is associated with its root compilation unit. The native module identity
includes this key. A comment-only owning-source or included-header change
therefore invalidates the module even when SimIR is identical, while changing
an unrelated, uninstantiated root retains the module object. Actual
VHDL scalar generic and SystemVerilog integral parameter/localparam values are
now constant-evaluated, canonicalized in declaration order, and included per
instance, including construction actuals transferred through an explicit
VHDL/SystemVerilog boundary binding. A separated VHDL entity source is also an
explicit specialization provenance dependency of its architecture. Complete
generic/parameter typing and SystemC construction schemas remain pending. The
LLVM object cache uses deterministic oldest-first eviction with simultaneous
age, entry-count, and encoded-byte limits. Successful loads refresh filesystem
recency; per-entry writer locks exclude active publications; stale temporary
files are removed under the same destination lock; malformed or unrelated
files are preserved; and empty canonical shards are reclaimed. Adapter
defaults are 30 days, 10,000 entries, and 10 GiB, with best-effort prune
telemetry that cannot prevent JIT construction. O0
exposes source-bearing statement, wait, assertion, process-entry, and
process-suspension and supported-call points plus addressable ≤64-bit packed
process locals. Richer local types and complete source metadata for deferred
object and executable kinds remain open.
The application analysis cache remains separate.

## Debug and public API

`include/fsim/api.h` defines opaque 64-bit session/object handles, versioned
append-only structures, diagnostic/status returns, hierarchy and value operations,
run/step/stop calls, and synchronous callbacks. It deliberately exposes no C++
layout and no exception may cross it. Every structure field is read or written
only when covered by the caller-advertised size; the original v1 object-info
prefix remains accepted and unknown future tails are preserved. Sessions can
currently load, check, and build projects; enumerate signals, ports, processes,
lexical scopes, and packed procedural variables; look up their hierarchical
paths; read,
deposit, force, and release values; run; step
by statement, process, delta, or time; request stop; and receive lifecycle,
safe-point, and value-change callbacks. Executable safe-point callbacks include
a valid process handle. Object handles carry a build generation so a rebuild
invalidates stale hierarchy handles, and mutating/rebuilding re-entry from a
synchronous callback is rejected. False assertions invoke the assertion
callback with the originating process handle plus severity, source
path/line/column, and message. Process, lexical-scope, variable, signal, and
port object metadata also carry retained declaration source locations. Scope
and local flags distinguish never-entered/uninitialized state from retained
runtime values. Elaborated child specializations are explicit instance scopes;
direct enumeration and signal/process parent metadata follow their owning
instance. Missing conditional/iterative hierarchy components are synthesized
as nested generate-region scopes, including indexed names and generated local
signal/process ownership. A generated process whose internal name equals its
scope receives a stable `.$process` public path. Each supported process/output
relationship is a generation-safe driver object beneath its signal, deduplicated
across repeated whole/slice blocking, update-phase, and delayed writes. Driver
reads return the issuing process's current pre-resolution slot while driver
mutation remains invalid. Resolved signal metadata and values are visible
through the same generation-safe C hierarchy.

An append-only detailed safe-point callback reports scheduler phase or
statement/call/wait/assertion/process-boundary kind, process handle,
time/delta, instruction index, and source location. The original callback
remains callable in parallel, while callers advertising the original v1
callback-structure size do not expose or trigger the extension.

Optimized `run` and instrumented `debug` are required to have identical
simulation semantics. Bounded debug code uses addressable process frames and
safe points at statements, supported calls, waits, process boundaries,
assertion failures, delta boundaries, and time boundaries. The default
LLVM-enabled `run` path is
the O2 hybrid engine. The current bounded `debug` path forces O0 for eligible
process groups and retains per-process interpreter fallback. SimIR carries
source-bearing statement, wait, assertion, process-entry, and
process-suspension points; O0 always returns them and O2 returns them only when
the size-gated runtime flag is enabled. A stable process-ID continuation
requeues an interrupted process at the same scheduler phase. An application
test runs the same source breakpoint/step/mutation command script through the
interpreter and O0 hybrid debugger and requires an identical transcript,
lifecycle, committed-change callback count, and final state. Distinct cold
objects beside the already populated O2 cache verify that the debug path
actually selected O0. Call instrumentation and the complete run/debug
differential remain release-gate work. The current REPL implements
`continue`/relative `run`, `run-until`, statement/process/delta/time stepping,
source/time/signal-change breakpoints with list/delete/clear operations and
exact-state signal `==`/`!=` conditions, hierarchy/scope navigation, signal
examination, and deposit/force/release. A configured debug VCD predeclares the
design signal table and permits live `add`/`remove`/`all`/`clear` selection;
enabling a signal records its current value and subsequent committed changes.
Run-mode VCD continues to declare only manifest-selected signals. A design
`$finish` marks the simulation finished, an external stop may be resumed, and a
fatal runtime exception poisons the simulation so later execution commands are
refused. Ctrl-C only sets an atomic stop request; the simulation thread observes
it at a safe point. The command-scoped signal-handler guard restores the host's
previous handler on every exit path. Tests raise SIGINT through the real handler
and require both the interpreter and O0 JIT debugger to stop at tick 0, resume
to terminal completion, and restore a preinstalled handler. The `locals`
command reads declared packed process variables through an engine-neutral
interface; richer local types remain planned.

## Tcl automation

`fsim tcl` embeds Tcl behind the application/CLI boundary. With no additional
argument it reads complete multiline commands interactively; repeatable `-c`
arguments and `SCRIPT [ARG ...]` provide deterministic batch forms. The host
replaces Tcl's terminating `exit` behavior with a returned process status and
connects Tcl stdin/stdout/stderr to the streams owned by the fsim invocation,
which keeps embedding and automated tests isolated from process-global C++
streams.

The namespace exposes version and project metadata, check/build, lexical
signal enumeration and packed reads, deposit/force/release, absolute-time or
completion runs, and lifecycle status. These commands own a lazily built
`BuiltProject`/`Simulation` pair and call the same application methods as the
CLI; they do not create another kernel. The same stateful adapter also exposes
breakpoint, stepping, trace-selection, diagnostic-query, mutation, and
synchronous callback commands over the common debugger and scheduler.

CMake accepts a Tcl 9.0 development package at patchlevel 9.0.4 or newer.
Older Tcl 8.6 packages, older Tcl 9.0 patchlevels, and other release series are
ignored. When no compatible package is present, the Tcl adapter downloads the
checksum-pinned 9.0.4 source archive, builds only the native static core, and
installs its headers and script library into the build tree. Installation
copies the runtime script library to `share/fsim/tcl9.0`; the embedded
interpreter locates it relative to the fsim executable so a staged
installation remains relocatable. The C++ boundary uses Tcl 9
`Tcl_Size`-bearing object commands and channel version 5 without legacy
integer-length narrowing. `FSIM_TCL_LIBRARY` overrides the standard-library
location for custom package layouts. `FSIM_TCL_MODE=OFF` is the explicit
opt-out.

## Platform boundary

The supported release targets are Linux x86-64 with GCC or Clang and Windows
x86-64 with MSVC or clang-cl. Filesystem, dynamic-library loading, process
invocation, Unicode path handling, and signal/console interruption stay behind
platform-specific boundaries. Public C, CLI, Tcl, project/cache, diagnostics,
and language-file paths use UTF-8; one conversion seam creates native
`std::filesystem::path` values, while Windows command-line and environment
inputs enter through UTF-16 APIs. SystemC source compilation passes argument
arrays directly to the selected GCC-like or MSVC-compatible toolchain and
never invokes a shell. A fingerprinted test launcher preserves any required
parent compiler discovery arguments without making the manifest cache-unsafe.
The current
compiler component produces checksummed, content-keyed shared libraries with
per-key locking, and project builds invoke it for SystemC source sets.
GCC-like builds use compiler-emitted dependency files and content-hash the
complete reported closure, including implicit system headers. MSVC and
clang-cl use `/sourceDependencies` JSON plus conservative roots for
unresolved constructs. Source content and path-addressed
linked inputs also participate in the key; options or inputs whose dependency
closure cannot be proved make a build non-cacheable. GCC-like tracked inputs
using `__DATE__`, `__TIME__`, or `__TIMESTAMP__` are likewise non-cacheable.
The compiler recomputes the plan and key after compilation and discards an
output when a tracked input changed before publication. A project build loads
the resulting library, checks `fsim_plugin_init_v1`, contains initialization
exceptions, and requires at least one valid factory registration. Typed
elaboration factories are constructed before HDL elaboration; their registered
ports and foreign HDL children are copied into ABI-neutral descriptions and
recursively incorporated into the common hierarchy in either direction.
Native module objects remain owned beside the built design until simulation
teardown. Facade-defined `SC_METHOD` callbacks are represented by dense common
process IDs and an external executor; registered port handles map to dense
signals, reads observe committed values, and writes enter the common update
phase. Static sensitivities therefore reuse the same fanout and next-delta
wakeup path as HDL processes. `SC_THREAD` and `SC_CTHREAD` keep their C++
stacks in Boost.Context 1.91.0 fibers while every context switch remains on the
single deterministic simulation thread. Wait callbacks yield a resumable
external process to the common scheduler; terminal shutdown resumes suspended
stacks with an explicit stop request before plug-in code is unloaded.

The facade's bounded fixed-width datatypes execute entirely inside plug-in
C++ code. `sc_bv` and `sc_lv` retain arbitrary compile-time widths while
`sc_uint` and `sc_int` cover widths 1 through 64 with explicit masking after
wrapping operations. Typed port and signal adapters canonicalize those values
at the plug-in boundary, so known and four-state results use the same packed
common-kernel representation, VCD path, and interpreter/LLVM-hybrid scheduling
semantics as HDL-produced values. No C++ datatype object crosses the native
plug-in ABI.

This cache boundary does not fingerprint every helper behind the selected
compiler driver or every environment-injected code-generation setting.
Options whose dependency closure cannot be proven therefore remain explicitly
non-cacheable; extensions such as `#pragma include_alias` are outside the
proven dependency model.

The checked portability inventory and 20-row differential corpus live in
[`v1-portability-audit.md`](v1-portability-audit.md) and
[`v1-portability-corpus.txt`](v1-portability-corpus.txt). Local builds use at
least eight workers and an eight-job Ninja link/archive pool. Hosted builds use
four workers; every MSVC-compatible test executable reserves an 8 MiB stack,
and separately named large tests retain explicit timeout and phase-trace
contracts. Batch 130 owns fresh hosted Linux/Windows execution.
