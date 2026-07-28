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
| Source manager | Files, source locations, include and macro ancestry | Exact ordered compilation-unit/transitive snapshots plus include/macro ancestry are current for Verilog/SV; VHDL source spans are current |
| Language frontend | Tokenization, preprocessing, parsing, name/type rules | Hand-written minimal VHDL and SV parsers plus a bounded multi-root SV preprocessor are current; typed semantic HIR is partial |
| Design elaboration | Specialization, hierarchy, bindings, drivers, stable IDs | Recursive VHDL/SV/SystemC hierarchy, dense instance-specific specialization records, bounded scalar VHDL generic and integral SV parameter specialization, executable conditional/iterative/selection generate expansion, construction-actual transfer across all three languages, explicit mixed bindings, port aliasing, and boundary checks are current; general generic/parameter typing and complete driver semantics are planned |
| SimIR lowering | Explicit reads, writes, waits, branches, assertions and yields | A typed executable subset is current |
| Reference engine | Execute any supported SimIR with deterministic scheduling | Current |
| LLVM engine | Compile each design-unit specialization and execute via ORC | The application groups eligible processes from each bounded elaborated specialization into one LLVM module while retaining typed per-process interpreter fallback; update/delayed writes plus dynamic/static sensitivity waits are current |
| Runtime | Time, deltas, resolution, callbacks, force/deposit and diagnostics | Scheduler, value changes, deposit, and a force/release mask are current; full driver/resolution model is planned |
| Visibility | C API, debugger safe points and VCD | Executable session API, VCD, and a scope/signal-oriented REPL with source/time/signal breakpoints, all four step modes, and bounded packed process-local reads are current; complete local scopes/types are planned |

The language-specific HIR will retain resolved symbols, types, overload choices,
constant values, and legality results. The common `DesignIR` will own dense
stable IDs for libraries, units, specializations, scopes, instances, processes,
signals, ports, drivers, source locations, and debug-visible objects. These
layers are still compacted together in parts of the current slice.

The compact elaborated design now assigns a dense specialization ID to every
instantiated unit occurrence and records its canonical unit identity, instance
path, directly owned process IDs, and canonical bounded VHDL generic or
SystemVerilog parameter/localparam values. Those values distinguish occurrence
and native-cache identity. Typed bounded scalar construction values also cross
SystemC factory boundaries in both directions. Complete generic/parameter
typing and reusable code-specialization deduplication remain planned.

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
is diagnosed with its package chain. Package bodies, types/subprograms,
standard-package loading, and general VHDL visibility remain future semantic
layer work.

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
SystemVerilog replication concatenations require a positive specialized
constant count and a statically sized nonempty operand group. The lowerer
builds the repeated value with binary doubling, requiring logarithmically many
two-operand `Concatenate` operations rather than materializing one operand per
copy. Expanded widths are checked against the SimIR limit before allocation.
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
An unguarded VHDL block or named SystemVerilog `begin : label` static region
contributes its label as a stable hierarchy component. Direct items inside an
explicit SystemVerilog `generate` region use one empty static parent scope, so
they retain module-scope names while their nested generated regions inherit
the parent's constant and object environments.
Same-language children resolve within the parsed units. A manifest
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
both directions. Expression port actuals, unpacked/record boundaries, and
actual multi-driver resolution remain outside this slice.

The v1 hierarchy is deliberately bidirectional for SystemC. An HDL instance
path may bind to a registered SystemC factory. During its elaboration, a
SystemC factory may register a named, typed foreign-child placeholder; the
manifest binding at that full path resolves the placeholder to a VHDL
architecture or SV module. The common elaborator remains authoritative in
both directions and supports recursive alternation between languages while
retaining one stable-ID namespace, one port-conversion policy, and recursion
detection. Facade modules create these placeholders with
`fsim::systemc::hdl_instance`; the native ABI remains its implementation
mechanism rather than the user-facing construction interface. Foreign
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

`hdl_instance::set_actual` records named signed scalar construction values on
the foreign-child descriptor through an append-only host callback. The common
elaborator applies them to the explicitly selected HDL unit using that target
language's name and subtype rules before port checks. They therefore flow into
the same canonical specialization and native-cache identity as source-written
generic/parameter actuals. The reverse HDL-to-SystemC direction now uses
factory-declared typed schemas and construction-value delivery.

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
allocation-free word path for reads and blocking, update-phase, and delayed
writes, while preserving signal and width validation at the boundary.

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

The same ordered token stream carries Verilog compiler state across shared
roots. `` `default_nettype`` selects scalar implicit-net and untyped-port net
types, with `none` producing a source diagnostic. `` `celldefine`` marks each
following module and that bit is retained on its elaborated specialization.
`` `unconnected_drive`` is captured on each instance and materializes an
omitted input as a `pull0`/`pull1` initialized signal. `` `resetall`` restores
time, net, cell, and unconnected-drive defaults. `` `begin_keywords`` scopes
the IEEE 1364/1800 reserved-identifier set until its matching
`` `end_keywords``. The current runtime executes these net forms with one
four-state driver; wired resolution, trireg charge storage, `` `line`` source
remapping, and standardized pragma behavior remain incomplete.

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
- unary/logical/reduction operations plus typed bitwise, fixed-width
  arithmetic, shift, conditional-select, and comparison operations;
- whole and normalized partial blocking writes, update-phase writes, and
  delayed writes;
- timed, dynamic-signal, static-sensitivity, and combined event-or-timeout
  waits;
- next-delta yields;
- jumps and branches;
- assertions; and
- process halt and simulation stop.

Bounded frontend lowering reaches these suspension operations from VHDL bare,
`on`, `until`, and `for` wait clauses, SystemVerilog integer `#` delay and
any-change/`posedge`/`negedge` `@(signal-list)` statements, and static process
sensitivities. Dynamic and static edge waits share the same four-state edge
predicate. VHDL condition waits suspend before their first condition test.
Combined event/timeout waits retain one absolute deadline while false event
wakeups rearm the sensitivity set; an internal scalar frame register records
whether the eventual wake was the timeout. A VHDL process containing explicit
waits jumps back to its post-initializer entry when its body completes,
preserving implicit process repetition without reinitializing locals.

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
identical. `case inside` and `unique`/`unique0`/`priority` qualifiers remain
targeted unsupported forms.

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

Bounded SystemVerilog conditional expressions lower to a typed SimIR select.
A scalar `0` or `1` chooses its corresponding equal-width alternative. An
`X` or `Z` condition compares the alternatives bit by bit, preserves identical
four-state bits, and produces `X` where they differ. This operation has the
same interpreter and allocation-free LLVM single-word implementation.
Vector truth conversion and the standard's complete expression sizing rules
remain pending.

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

Logical conjunction and disjunction reduce each operand independently, so
packed operands need not have the same width. The resulting scalar uses the
standard four-state controlling-value rules: a known false controls `&&`, a
known true controls `||`, and `X` is produced only when neither controlling
value determines the result. Supported operand expressions are currently
side-effect free; observable function/task short-circuit behavior remains
pending with executable calls.

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
objects. Unpacked or multidimensional arrays remain pending.
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
static negative counts reverse the operation direction (`sll`/`srl`,
`sla`/`sra`, or `rol`/`ror`) before lowering the absolute magnitude. Dynamic
VHDL shift counts remain pending.

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
update-write, and delayed-write callbacks. The `write_update` and
`write_after` callbacks are an append-only extension of the v1 table: original
field offsets remain fixed, and each compiled process checks `struct_size` only
for the callback tail it actually uses. A process using only the original
operations therefore remains valid with the original v1 prefix. CMake requires
the exact supported LLVM package when `FSIM_LLVM_MODE=ON`; the checked-in Linux
LLVM job builds and runs the adapter suite against 22.1.8. A separate C11 test
verifies the offsets, extended size, callback handoff, and genuine C ABI.
`WAIT_ON`, `WAIT_SENSITIVITY`, `DEBUG_POINT`, and `WAIT_FOREVER` are append-only
resume-status values 6 through 9; values 0 through 5, the v1 result ABI
version, and the 24-byte result layout are unchanged. The existing instruction
field identifies the immutable SimIR boundary operation, and delay remains
meaningful only for `WAIT_FOR`.

The current adapter compiles control-flow graphs containing loads, reads,
common operations, blocking writes, assertions, jumps, branches, timed waits,
dynamic-signal waits, static-sensitivity and permanent waits, next-delta yields,
update-phase writes, delayed writes, design stop, and halt.
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
cross the native ABI; any operation that reads or writes a value remains on the
1-to-64-bit compiled fast path.

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
scheduled-write kind/delay invalidation, wait-kind/operand invalidation, and
optimization-mode invalidation are also tested at O0 and O2.

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
cache has no age/size eviction policy. O0
exposes source-bearing statement, wait, assertion, process-entry, and
process-suspension points plus addressable ≤64-bit packed process locals. Call
points, complete local scopes/types, and complete source metadata remain open.
The application analysis cache remains separate.

## Debug and public API

`include/fsim/api.h` defines opaque 64-bit session/object handles, versioned
structures, diagnostic/status returns, hierarchy and value operations,
run/step/stop calls, and synchronous callbacks. It deliberately exposes no C++
layout and no exception may cross it. Sessions can currently load, check, and
build projects; enumerate the bounded signal/process object view; look up
hierarchical signal paths; read, deposit, force, and release values; run; step
by statement, process, delta, or time; request stop; and receive lifecycle,
safe-point, and value-change callbacks. Executable safe-point callbacks include
a valid process handle. Object handles carry a build generation so a rebuild
invalidates stale hierarchy handles, and mutating/rebuilding re-entry from a
synchronous callback is rejected. False assertions invoke the assertion
callback with the originating process handle plus severity, source
path/line/column, and message. Scope objects, C API local objects, and complete
non-assertion source/debug metadata are not yet wired.

Optimized `run` and instrumented `debug` are required to have identical
simulation semantics. Bounded debug code uses addressable process frames and
safe points at statements, waits, process boundaries, assertion failures,
delta boundaries, and time boundaries. Call points remain open. The default
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
interface; nested scopes, richer types, and C API local objects remain planned.

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

This cache boundary does not yet fingerprint every helper behind the selected
compiler driver or every environment-injected code-generation setting. The
MSVC fallback also does not consume `/sourceDependencies`, so extensions such
as `#pragma include_alias` are outside its proven dependency model.
