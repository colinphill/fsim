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

The large table below is the accumulated v1 slice and contains historical
limitation text. The dated v2 status sections later in this document supersede
those limitations. In particular, the Batch 151 arbitrary-width status update
is authoritative for SystemVerilog packed constants, aggregates, callables,
runtime services, and artifacts; a remaining 64-bit statement is a limit only
when that update explicitly retains it for a particular operation.

Batch 164 is authoritative for Verilog-2005 literals: bit strings, based
numbers, and derived packed values have no arbitrary language-width cap.
Signedness and `X`/`Z` state remain exact across both engines, VPI/public
values, traces, artifacts, relocation, and caches. The explicit VPI `uint32_t`
descriptor width and governed memory/work/trace budgets are physical host or
resource ceilings, not Verilog legality limits.

Batch 166 currently makes VHDL-87, VHDL-93, VHDL-2000 and VHDL-2002 explicit
selectable source profiles while retaining VHDL-2008 as the default. The
selected revision is preserved through source and analyzed logical-library
identity, and incompatible dependency use or reanalysis is rejected instead of
being silently interpreted under another revision. Revision-specific grammar
and predefined environments follow the selected typed revision.
Lexical selection is revision-correct: VHDL-93, VHDL-2000 and VHDL-2008
reserved-word additions remain identifiers before their introduction;
extended identifiers begin in VHDL-93; and VHDL-2008 delimited comments,
question-mark/external-name delimiters, explicit bit-string lengths, decimal
bases, signed/unsigned bases and non-numeric bit digits receive exact-span
migration diagnostics in older modes. Legacy B/O/X literals and arbitrary
bit-string widths remain exact. Declaration selection also enforces the
VHDL-93 introduction of shared variables, groups, alias signatures and modern
file opening, the VHDL-2000 introduction of protected types, and the VHDL-2008
introduction of type, subprogram and package interfaces plus unconstrained
array element subtypes. VHDL-87 `is in`/`is out` file objects normalize to the
same typed open-kind HIR as later declarations. Unavailable features use
`FSIM-FE-VHSTD-003`; malformed forms retain their parser diagnostics. Exact
array bounds, direction and arbitrary-width aggregate values are unchanged.
VHDL-1993 legacy unprotected shared variables execute as one bounded
scalar/packed shared identity with static initialization and deterministic
source-ordered process access. VHDL-2000, VHDL-2002 and VHDL-2008 instead
require a protected type; the legacy form receives
`FSIM-ELAB-VHPROTECTED-008`. Protected private storage and public method calls
execute under both older protected-type revisions. Methods are atomic because
the bounded executable profile rejects suspension and reentry, and every fresh
simulation reconstructs private/shared initial state. Interpreter and LLVM
produce the same VHDL-1993/2000/2002 results without weakening multi-driver
checks for actual signals.
Expression selection is revision-correct as well: `xnor` and the six shift/
rotate operators begin in VHDL-93, while unary logical reductions, `??`,
matching equality, conditional/case expressions, external names and direct
selection/indexing of function results require VHDL-2008. Qualification,
conversion, named association, universal integer actuals and null array ranges
remain available in their older owning revisions. Expected result types
participate in deterministic overload selection; ambiguous package-style
profiles reject explicitly. Interpreter and LLVM execution retain exact
137-bit results, and zero-width null-array frames do not acquire host-word
storage.
Structural selection is revision-correct. VHDL-87 retains component
instantiation, positional/named port maps, explicit sensitivity lists,
packages and configuration declarations. Direct entity/configuration
instantiation, postponed concurrent statements and standalone `report` begin
in VHDL-93. Context declarations/references, `process(all)`, force/release,
sequential conditional/selected assignments, matching case/select, case
generate and if-generate alternatives require VHDL-2008 and receive an exact
migration diagnostic in older modes. Nonstatic input-port expressions require
VHDL-2008; older modes retain whole/selected signal names, historical
single-argument conversion interpretations and static values. A VHDL-93
component/direct-entity application executes an exact selected 137-bit port
actual without narrowing, while the same revision rejects a dynamic composed
port expression that VHDL-2008 elaborates.
Each VHDL unit owns a canonical `ieee-1076-standard:<year>:fsim-v1`
environment with its working library, implicit `std`/`work` and
`std.standard.all` visibility, exact standard declarations and operator
profiles, the `fs` through `hr` time-unit ladder with `fs` as the default, and
revision-correct attributes. File status types, `xnor`, shifts/rotates,
image/value and the signal/name attribute family begin in VHDL-93. Standard
Boolean/integer/real/time vector types, reductions, condition/minimum/maximum
operators and subtype/element attributes remain VHDL-2008-only. Compiler-owned
IEEE packages are parsed under the requesting source's revision, later package
families are rejected at their owning use clause, and one project cannot mix
incompatible revisions of the same intrinsic IEEE environment. Older modes do
not inherit VHDL-2008 string-conversion intrinsics such as `to_hstring`.
Compiler-owned IEEE and Synopsys package projections are parsed under every
older selectable revision. The
historical `ieee.std_logic_signed`, `std_logic_unsigned`, `std_logic_arith` and
`std_logic_misc` profiles are explicitly non-standard Synopsys compatibility
dependencies; selecting an older VHDL revision alone does not make them
visible. An explicit use clause injects a clean-room declaration projection
with the historical package name, type/subtype names, conversion/reduction
functions and exact overload profiles. Each analyzed projection records the
package name, selected VHDL year and `synopsys-legacy-ieee:1990-1992` origin in
its compiler-owned revision identity. VHDL-1993 and later projections coexist
deterministically with `numeric_std`; redeclaration and incompatible projection
fail at the owning use/source boundary. The clean-room executable profile
implements the historical arithmetic, comparison, conversion, extension,
shift and reduction families without a host-word width cap. A visible
`std_logic_signed` or `std_logic_unsigned` package controls the interpretation
of ordinary `std_logic_vector` arithmetic and comparison in its owning design
unit, including ascending and descending constraints. `std_logic_arith`
conversions retain exact arbitrary-width results, `std_logic_misc` reductions
propagate standard-logic unknowns and use the historical identities on null
vectors, and zero-width built-in vector constraints remain concrete through
elaboration. Simultaneously importing signed and unsigned vector overloads is
not resolved by preference: a genuinely conflicting expression fails with
`FSIM-ELAB-VHSYN-001`; use one package, qualify `conv_integer`, or convert to an
explicit standard numeric type.

The revision-indexed conformance corpus is published in
`tests/feature_matrix/vhdl_revision_corpus.tsv`: every VHDL-87, VHDL-93,
VHDL-2000 and VHDL-2002 row owns a legal older semantic witness and an exact
negative witness for a later construct. The companion
`vhdl_synopsys_package_corpus.tsv` assigns every compatibility package its
positive execution, negative diagnostic, arbitrary-width and revision-
provenance evidence. The registered inventory gate verifies every evidence
file and anchor so the published support table cannot drift from executable
coverage.

The serial `fsim.vhdl-standard-mode-closure-matrix` composes that corpus with
the frontend, all four revision/package environments, arbitrary-width numeric
execution, older-mode mixed SystemVerilog execution, artifacts, LLVM, public
C/C++/Tcl services and the MSVC/Windows/tool/resource contracts. It retains a
stage ledger and one verbose log per witness. Exact transcript tokens bind all
four years and packages, interpreter/LLVM O0/O2, 137-bit ascending/descending
operations, historical null reductions, ambiguity rejection, debugger/VHPI/
VCD provenance, caches, relocation, replay and checkpoints. Each owning
execution process applies the portable 6 GiB ceiling; delta-1000, VCD-64,
1,200-second stage and 7,200-second serial-matrix limits are resource ceilings,
not VHDL legality or width limits.

Batch 166 cache identity independently binds the selected canonical VHDL year
and the compiler-owned `fsim-synopsys-ieee-compat-v2` profile. `.fsimlib`
format 2, `.fsimobj` format 4 and `.fsimdesign` format 6 retain each selected
Synopsys package name, revision-specific predefined-environment identity and
exact clean-room source digest. `.fsimdesign` format 7 additionally indexes
that identity per semantic VHDL unit, so standalone debugger, VHPI, activity
and VCD provenance remains available after source/object removal and design
relocation. Object and design provenance digests cover the complete dependency
records. Reload regenerates the current compiler packages and rejects stale,
unavailable, omitted, unordered, unit-inconsistent or digest-mismatched
dependencies with `FSIM-ART-VHDEP-001`, `FSIM-ART-0011` or `FSIM-ART-0014`; it
never silently substitutes a newer implementation. Verilog and SystemVerilog
explicitly use the `none` compatibility profile. An identical VHDL profile
reuses the same native object, while changing the year, compatibility
implementation, package source or design provenance produces a deterministic
miss. Unsupported future object/design schemas retain the established
`FSIM-ART-0001` and `FSIM-ART-0010` diagnostics.

Older-revision execution uses that retained identity end to end. VHDL-87,
VHDL-93, VHDL-2000 and VHDL-2002 component hierarchies execute on both the
interpreter and LLVM engines at O0 and O2 when either a SystemVerilog parent
drives a VHDL child or a VHDL parent drives a SystemVerilog child. The common
boundary retains all Logic9 planes during Logic9-to-Logic4 projection, while
the owning VHDL process retains ascending and descending 137-bit bounds and
the `std_logic_unsigned` overload selected by its source revision. The same
scheduler preserves time and delta ordering through the existing multiple-root
VHDL/SystemVerilog/SystemC boundary. Optimization and language conversion do
not select a different compatibility overload or reinterpret array direction.

Public VHDL introspection now retains that same identity. Each elaborated
VHDL scope exposes its canonical year, predefined-environment identity,
compatibility profile and selected compiler-package name/revision through the
bounded debugger snapshot and VHPI object metadata. Execution-point activity
inherits the year/profile from the owning process, and VCD output emits stable
scope/unit/source provenance comments. Exact 137-bit values remain mutable
through deposit/force/release without narrowing. Compiler package projections
remain dependencies rather than user hierarchy: neither their logical package
names nor their implementation sources are published as discoverable scopes.
Interpreter and LLVM O0/O2 runs produce the same public provenance for every
older selectable revision.

Batch 165 is authoritative for the governed SystemVerilog-2017 boundary. Its
30 supported clause/integration rows and 21 preserved width paths have zero
active residual rows; five explicitly deferred extensions keep their later
batch or post-v2 owners. The 6-GiB process, 1,000-delta, 64-trace-signal, and
time ceilings govern release evidence only. They are not SystemVerilog
legality or packed-width limits. Exact inventories and their digests are in the
[SystemVerilog release audit](v1-systemverilog-release-audit.md).

| Area | Parsed now | Executable now | Important limitations |
|---|---|---|---|
| VHDL units | `library`/`use`/context-reference clauses retained on their following unit; reusable context declarations containing bounded context items; package declarations containing bounded constants, subtypes, non-nested records, user-defined enumerations, bounded multidimensional arrays with scalar/vector/record/enumeration/nested-array elements, and scalar function/procedure declarations, plus matching bounded package subprogram bodies; bounded generic package declarations and entity/architecture-local package instantiations over existing scalar value/type/function/procedure generic families; bounded generic function/procedure templates and local or package-visible instantiations over those families; entities and architectures with package/entity/architecture type and subtype declarations over scalar logic/bit/Boolean, enumerations including ascending/descending constraints, constrained signed/unsigned or logic/bit vectors, constrained or `integer`/`natural`/`positive range <>` user arrays, portable integer ranges, and bounded records; scalar integer/Boolean/bit/enumeration or subtype-typed generics; VHDL-2008 unclassified interface type plus bounded interface function, procedure, and package generics; parameterized packed and enumeration ranges; record/subtype/enumeration/user-array or packed scalar/vector ports, signals, and bounded packed process variables; and `integer`/`natural`/`positive`/explicit integer-range ports, signals, and process variables; direct-entity and component-style instances with positional-then-named `generic map` actuals including `open` value-default selection, labeled `if`/`else`, integer-range `for`, scalar/inclusive-range-choice `case` generate regions, and labeled block statements with optional Boolean guards and implicit reactive `GUARD` signals containing bounded constants, local packed signals, concurrent assignments, processes, instances, and nested regions; block interfaces may bind bounded value/type/function/procedure/package generics and generic-dependent ports with positional-then-named maps, defaults, and `open` | Recursive acyclic `context library.name` expansion; explicit use visibility and direct `package.item`/`library.package.item` references for declaration-ordered scalar constants and bounded record/subtype/enumeration/array declarations and literals, including precise transitive context/package source provenance; directly visible package functions and procedures merged from matching bounded bodies; independently resolved entity and architecture type regions with entity subtype visibility in the associated architecture; chained subtype resolution, derived integer/enumeration-base containment, packed reconstraint legality, specialization-dependent packed/enumeration/array bounds, subtype-left defaults, checked enumeration constants/generics/stores, nominal user-array values, and directionally safe hierarchy aliases; declaration-order record layouts and minimum-width nominal enumeration ordinals, element-domain defaults, same-language nominal port aliases/copy/comparison, enum-typed constants/generics, contextual identifier/character literals and case choices, record/array subtype aliases, contextually typed positional/named/final-`others` record aggregates and recursive positional/discrete/range/choice-list/final-`others` multidimensional array aggregates, multidimensional member/index/slice access, persistent debug-visible record/subtype/enumeration/array locals, and explicit mixed-boundary rejection; recursively elaborated per-occurrence value/type/function/procedure/package generic specializations with named/positional whole-signal plus bounded static/dynamic expression and `open`/default `port map` associations, declaration-ordered local package and generic-subprogram specialization, selected package constants/types/functions/procedures, exact interface-package and instantiated-subprogram forwarding with transitive body provenance, declaration-ordered generated-constant and pure-function folding, interval-based selection with null-range handling, specialization-selected or always-selected scoped behavior with scope-qualified locals, delayed block-interface binding after enclosing local-package specialization, persistent process-variable registers, and signed 32-bit two-state integer-family objects with specialized constraints, subtype-left initialization, and range-safe aliases | Complete project package bodies and general package subprograms, general or nested generic package units, nested generic subprogram templates, unreviewed standard-library packages, and general visibility/overload resolution are not implemented; record types remain non-nested and packed and exclude integer/access/protected elements, nested aggregates, element-choice groups, and qualified aggregate expressions; user arrays support bounded multidimensional scalar/vector/record/enumeration/nested-array elements, recursive aggregates, null executable objects, and same-language callable/hierarchy boundaries but exclude qualified aggregate expressions, dynamically chosen aggregate associations, unbounded runtime-sized slices, and cross-language composite values; enumeration literal visibility is currently contextual rather than a complete overload candidate-set implementation; package/context visibility cycles are rejected; no configurations, mixed-language interface-package/subprogram actuals, VHDL-2019 classified interface types, process-local subtype declarations, nonintegral case-generate choices, generated variable/file/alias/attribute/use/group declarations, general qualified/function-call or dynamically composed aggregate port actuals, or process declarative items beyond bounded variables; scalar constraints outside the portable signed 32-bit interval are rejected |
| VHDL statements | Concurrent assignments, labeled/unlabeled `with`/`select` selected signal assignments and concurrent assertions, process sensitivity lists, `if`/`elsif`/`else`, ordered packed `case`/`when`/`others`, locally static sequential `for`, Boolean `while`, unconditional and labeled loops, conditional/targeted `exit` and `next`, simple and chained VHDL-2008 conditional signal/variable assignments, signal/variable assignment including one-level record-element targets, `null`, ordered waveform elements, `unaffected` alternatives, exact integer `after`, implicit/explicit `inertial`, `transport`, and `reject TIME inertial`, bare/`on`/`until`/`for` wait clauses in every legal combination including nested procedure bodies, sequential `assert` with optional general string `report` and `severity_level` expressions, and sequential `report` statements | Whole-record and whole/constant-selected packed signal/local assignments, contextually typed record and recursive multidimensional/composite array aggregate initializers and whole-object values, constant or executable multidimensional record-member/index/slice writes, dynamic single-element and fixed-width runtime-slice signal/local writes with signed 32-bit `integer`-family bounds and execution-time target capture for delayed/projected waveforms, source-ordered Boolean conditional-assignment alternatives including record aggregates, selected concurrent assignments with grouped exact choices, final `others`, independent ordered waveform lists or `unaffected` on every conditional/selected alternative, and inferred reactive sensitivity; complete new signal waveforms atomically edit cancelable projected transactions per scalar subelement, with transport truncation and exact inertial mark/delete using an explicit rejection limit or the first delay by default; guarded simple/conditional/selected assignments in Boolean-guarded blocks retain one reactive driver identity each and route false-guard or explicit-`null` nine-state values through the same delayed projected-waveform path as resolution-neutral `Z`; nested Boolean-typed conditional branches, exact `|`-separated case choices with nested statement lists, ascending/descending/null `for` ranges with implicit-constant substitution and bounded unrolling, executable loop backedges with persistent locals, nested/named loop transfers, edge-guarded clock processes, waits nested through conditionals, loops, and exact overload-resolved procedure chains, first-suspending condition waits, event-or-timeout races with preserved absolute deadlines, Boolean assertions and sequential reports with runtime string concatenation and severity values, skipped passing-expression evaluation, exact source metadata, and nonfailure severities continuing, reactive concurrent-assertion sensitivity, integer/logic/vector/Boolean literals, and selected operations | Bounded selected assignments require a final `others` and do not support matching-select `?`; guarded/null disconnection is limited to nine-state signal targets and does not implement disconnection specifications or arbitrary resolution-function driver removal; case ranges, qualified aggregate expressions, unbounded runtime-sized slices or selection beyond the supported multidimensional/record chains, general nested/chained record selections or local scopes, configurable assertion stop levels, dynamic time-valued objects, or nonstandard physical-time units remain unsupported; standard physical time is normalized exactly and waveform times must be strictly ascending |
| VHDL expressions | Identifiers and bounded multidimensional array/record selected names, decimal/logic/string/Boolean literals, identifier/character enumeration literals, record and array aggregates, calls, index/slice syntax, common unary/binary syntax | Identifiers/literals and objects resolved through bounded scalar/vector/record/enumeration/user-array subtype chains; local and rooted dot-separated VHDL-2008 external signal names resolve to the existing signal identity with exact subtype validation; contextual enumeration literals in initializers, assignments, conditional alternatives, comparisons, and case choices with nominal identity and ordinal relational ordering; enumeration type/subtype-mark `left`/`right`/`low`/`high`/`length`/`ascending` and checked `pos`/`val`/`succ`/`pred`/`leftof`/`rightof` over each resolved ascending or descending subtype range, including package/generic folding, base-declaration ordinals, and dynamic failures; contextually typed positional/named/final-`others` bounded record aggregates in initializers, whole-object assignments, equality/inequality, and conditional alternatives; whole nominal user-array copy/equality, recursive multidimensional/composite aggregates, null values, constant or executable multidimensional indexing/slicing and supported record/array chains, dynamic single-element indexing, and context-sized 1-through-64-bit runtime slices with signed 32-bit `integer`-family bounds, declared-range mapping, exact direction/length checks, and packed record-member chains; record-member reads with constant element bit/slice selection, unary plus/minus, checked integer-family and bounded packed signed `abs`, Boolean and packed `not`/`and`/`or`/`xor`, Boolean `nand`/`nor`/`xnor`, equal-width signed/unsigned packed arithmetic plus checked signed 32-bit integer-family `+`, `-`, `*`, `/`, `rem`, `mod`, and locally static nonnegative `**`, equality/inequality/relational comparisons including whole same-record-type equality, general branch-short-circuited conditional expressions including arbitrary-width static packed alternatives, bounded case expressions with choice lists, scalar ranges, exhaustive Boolean alternatives, and final `others`, packed `sll`/`srl`/`sla`/`sra` and `rol`/`ror` with locally static or dynamic integer counts and negative-count reversal, constant in-range indexed names/slices with declared-range mapping, and width-summing packed `&` concatenation | External variable/constant names, relative parent paths, generated-path indices, selected/qualified type marks in attribute prefixes, qualified expressions, dynamic/negative integer exponentiation, unbounded runtime-sized slices or selections beyond the supported chains, most calls/operators, dynamically chosen aggregate associations, and complete overload/self-determined sizing are not lowered; out-of-range or unknown dynamic indices and invalid runtime slice bounds fail deterministically; explicitly mixed signed/unsigned numeric operands require conversion |
| Verilog/SV units | Modules; bounded packages containing integral parameters/localparams, packed integral typedef aliases, packed enums, recursive packed structs, equal-width ordinary packed unions, tagged unions, imports, bounded functions, and bounded tasks; compilation-unit or unit-local wildcard/selected imports; ANSI and classic callable formals plus named/default actuals; ANSI and basic non-ANSI packed ports plus integral static-array, dynamic-array, queue, bounded-queue, and integral-key associative-array ports, nets/variables, packed constant or parameterized ranges, integral value parameters/localparams using implicit, `byte`, `shortint`, `longint`, `time`, `int`/`integer`, or packed `bit`/`logic`/`reg` types with explicit signedness, same-language `parameter type`/`localparam type` declarations, module instances with named or positional value/type overrides, explicit or implicit conditional/inline-or-module-`genvar` iterative/constant-choice generates, and direct or named static contents in explicit generate regions containing bounded parameters/localparams, local packed signals, functions/tasks, continuous assignments, processes, instances, and nested regions | Recursive case-sensitive package imports, `package::constant` folding, imported/scoped/local typedef resolution, enum enumerator visibility, exact two-/four-state scalar defaults, arbitrary-width enum values, declaration-order packed-struct layout, offset-zero ordinary packed-union overlays, tagged-union payloads, nominal assignment compatibility and arbitrary-rank unpacked dimensions; typed arbitrary-width parameter defaults, overrides, localparams, and non-iterative generated parameters preserve width, signedness, state domain and X/Z planes after specialization-dependent range folding; type parameters resolve integral builtins, local/imported/package-selected typedef marks, dependent packed ports/signals/typedefs/value constants, nested formal forwarding and 137-bit actuals with `sv-type-v3` cache identity; bounded module/package/generated automatic functions and tasks admit integral, byte-string, dynamic/queue/associative-container, and locally constant static unpacked-array values, with module objects/automatic locals, whole or direct compatible slice value-copy actuals, nested nonrecursive calls, suspension-safe atomic copy-out, and transitive source provenance; direct same-language whole-container port actuals preserve exact specialized element, kind, queue-bound, associative-index, or static-range metadata; whole packed aggregates and constant or chained compatible member selections execute through common exact-width SimIR operations; arbitrary-width integral scalar, enum, packed-struct, and packed-union associative indices retain exact value planes and signed ordering | Complete LRM expression typing, genvar-dependent typed constants, generated type declarations, unrestricted task/function forms, general expression or runtime-variable/dynamic-container port actuals, cross-language containers, suspending/static ref callables, standard descriptor aliases, multichannel descriptors and unrestricted host I/O/allocation remain outside this row |
| Verilog/SV statements | `assign`, bounded scalar and static-array built-in gate primitives, event-controlled or path-safe body-timed `always`, edge-controlled `always_ff`, and inferred `always @*`/`always_comb`/`always_latch`, `initial`, SystemVerilog `final`, blocks, leading packed procedural variables, `if`/`else`, `case`/`casez`/`casex`, bounded `case inside` with comma-separated choices, and bounded `case matches` with one constant or `.*` pattern per item, `default`, and optional SystemVerilog `unique`/`unique0`/`priority` qualifiers, bounded inline or external-variable procedural `for`, locally static or runtime integral `repeat`, runtime `while`/`do-while`, and path-safe suspending or exiting `forever`, nested `break`/`continue`, blocking/NBA assignments, SystemVerilog compound assignments and standalone prefix/postfix increment/decrement, procedural force/release, exact decimal/scientific and locally constant integral parameter/localparam delays, parenthesized `min:typ:max` delays, one/two/three-value continuous-assignment transition delays, one/two-value logic-gate and one/two/three-value tri-state-gate delays, direct any-change/`posedge`/`negedge`/wildcard controls, exact general packed any-change expressions, and repeated intra-assignment event controls, condition waits, `$stop`, `$finish`, standalone `$info`/`$warning`/`$error`/`$fatal`, immediate assertions with simple or lexical-block pass/failure actions, the nonsuspending statement subset inside bounded automatic/static functions, and the supported scheduler controls inside bounded automatic tasks plus nonsuspending static tasks, bounded SystemVerilog-2017 `$fopen`/`$fclose`/`$fdisplay`/`$fwrite`/`$fgets`/`$feof`/`$ferror` text-file forms, and `$readmemb`/`$readmemh` into bounded static memories | Whole, chained static packed, constant bit/part-selected, constant or runtime-base `+:`/`-:` indexed-selected, and dynamic single-bit packed signal/local assignments use one checked lvalue capture; partial runtime-base writes update only representable bits, while unknown or wholly out-of-range bases do nothing; dynamic delayed/NBA targets capture their selection at assignment execution. SystemVerilog compound assignments accept delay/event controls and evaluate the target once; expression and statement prefix/postfix `++`/`--` retain new/old result ordering. Whole, packed-member, static-bit, and static-part signal force/release masks only the selected region while underlying drivers continue. Bounded function bodies execute blocking local assignments, blocks, conditionals, exact case, canonical loops, break/continue, nested nonrecursive calls, function-name assignment, value return, typed named/default inputs, and packed output/inout/ref transfer; bounded task bodies add statement delays, named-event waits/triggers, and condition waits to the shared control subset with nested function/task calls, valueless early return, persistent formals/locals, named/default actuals, direct-local bounded ref, and deferred ordered input/output/inout copy-in/copy-out; `$stop` pauses before the following statement and resumes after the application clears the stop; severity tasks retain an optional bounded literal message, with note/warning/error continuing and `$fatal` accepting an optional ignored numeric finish control before terminating; final procedures execute exactly once after ordinary quiescence or `$finish` and may contain the supported nonsuspending blocking statement subset; comma-separated optionally named `buf`/`not`/`and`/`nand`/`or`/`nor`/`xor`/`xnor`/`bufif0`/`bufif1`/`notif0`/`notif1` primitives, plus resource-governed static arrays with direction-aware scalar/vector terminal mapping, lower to indexed common four-state continuous processes; inline `int`/`integer` or external-variable procedural loops with integral `<`/`<=`/`>`/`>=` bounds, positive constant steps, deterministic static unrolling or runtime backedges, null ranges, and update-point `continue`; locally static or single-evaluated runtime integral repeat counts with negative and unknown values producing zero iterations; executable pre/post-test loop backedges with nested control transfers; path-safe timed, event-controlled, terminating, or deterministic-break `forever`; body-timed `always` re-entry only after a proven suspension or termination; nested `if`/`else` with packed four-state truth conversion; transitive cycle-safe wildcard dependencies through visible function/task bodies; time-zero `always_comb`/`always_latch`; ordered exact, symmetric selector-or-choice wildcard, right-choice-wildcard value/range `case inside` matching, and exact constant or unconditional-wildcard `case matches` matching, plus source-aware alternative-level `unique`/`unique0`/`priority` checks; exact packed-expression value-change filtering, repeated event suspension, direct edge suspension, and immediate-test condition waits; packed-condition immediate assertions with implicit error and scoped pass/failure actions; delays inherit the active time context, select `min`/`typ`/`max`, then round to SystemVerilog precision; delayed continuous whole/slice writes and net-declaration propagation delays select rise/fall/turnoff from actual changed four-state elements, combine checked driver and net delays, use the shortest applicable packed transition, retain same-value transactions, and cancel superseded inertial updates; manifest-relative text and memory files use opaque process-owned services with deterministic byte/element bounds and interpreter/native lifecycle parity | SDF parsing and annotation are not implemented; `$stop` accepts but ignores its optional verbosity argument; final procedures reject timing controls, waits, `$stop`, `$finish`, and NBAs; nonprogressing `always`/`forever` paths are rejected; procedural `for` updates remain positive constant steps toward a direct comparison bound; edge-qualified general packed expressions and mixed general-expression event lists remain unsupported; runtime-selected targets followed by another selection, automatic process variables beyond the substituted loop index, nested or nonintegral static callable locals, suspending static tasks, nonlocal/suspending ref actuals, nonintegral writable function formals, binary/positioned/multichannel files and standard descriptor aliases, multidimensional or aggregate/string-element memories, unrestricted dynamic allocation, formatted/dynamic severity-task messages, or force/release of automatic locals and runtime-selected targets |
| Verilog/SV expressions | Identifiers, sized literals, strings, unary and common binary syntax, conditional (`?:`), index/part-select/concatenation syntax, call syntax | Identifiers/literals, unary plus/minus, bitwise complement (`~`), vector-aware logical negation (`!`), mixed-width logical and/or, unary and/or/xor reductions and their `~&`/`~|`/`~^`/`^~` complements, bitwise and/or/xor plus binary `~^`/`^~` XNOR, logical and arithmetic left/right shifts with signedness-sensitive four-state sign fill, left-associative fixed-width `**`, bounded SystemVerilog sizing/conversion, equal-width signed/unsigned add/subtract/multiply/divide/remainder and relational comparisons, equality/inequality with unknown propagation, exact known-result case equality/inequality (`===`/`!==`), right-operand-masked SystemVerilog wildcard equality/inequality (`==?`/`!=?`), short-circuit logical operators, equal-width conditional alternatives under a scalar condition with four-state bit merging and skipped-branch behavior, constant in-range bit/part selects, dynamic single-bit selects using the common signed 32-bit index representation, constant and runtime-base `+:`/`-:` indexed part-select reads with declared-range mapping, packed concatenations, bounded fixed-width integral left/right streaming concatenations, direct locally constant colon or indexed static-array selections in supported contextual assignment, consumer, callable, ordering, and module-port forms, checked constant replication concatenations of statically sized operands, bounded nonnegative integral `$clog2` constant calls, width/bit-preserving `$signed`/`$unsigned` casts, packed `$isunknown`, `$onehot`, and `$onehot0`, ordered simulation-owned `$test$plusargs`/`$value$plusargs` prefix and first-match queries with signed, string, real/scalar and arbitrary-width four-state conversion, the complete IEEE 1800 scalar `$rtoi`/`$itor`/raw-bit and logarithmic/exponential/power/rounding/trigonometric/hyperbolic math family, signed 32-bit `$countones` and constant-control `$countbits`, 32-bit `$bits` results for statically sized packed expressions and direct one-dimensional integral unpacked containers, signed 32-bit `$left`/`$right`/`$low`/`$high`/`$size`/`$increment` results with an optional constant dimension `1` over packed objects and supported unpacked containers, packed/unpacked `$dimensions`/`$unpacked_dimensions` counts, direct contextual positional/keyed assignment patterns for supported whole containers including bounded static-array default/index-key patterns, exact-element-type `sum`/`product`/`and`/`or`/`xor` reductions with an optional bounded pure `item`/`item.index` conditional transformation, no-argument `min`/`max`/`unique`/`unique_index` queue locators, bounded pure-predicate `find`/`find_index`/`find_first`/`find_first_index`/`find_last`/`find_last_index` queue locators on supported direct unpacked containers, and bounded local/imported/package-selected function calls with supported lifetime, positional/named/default associations, and packed writable/reference formals in eligible constant or runtime expressions | General aggregate/container streaming, runtime stream slice sizes, dynamic `$countbits` controls, query type references, indirect or multidimensional container queries or reductions, named or arithmetic/function/side-effecting reduction transformations, no-argument locator `with` clauses, type-keyed or nested assignment patterns and defaults outside direct one-dimensional static arrays, predicate iterator calls/side effects/arithmetic, dimension arguments other than `1`, arbitrary vector-valued/negative `$clog2` arguments, unrestricted function profiles and recursion are not lowered; DPI profiles outside the bounded Batch 156 surface are rejected; out-of-range or unknown dynamic indices fail at runtime |
| Preprocessing/directives | Quoted and angle includes, manifest/CLI definitions, object/function macros with default arguments, multiline replacement, argument substitution, token concatenation/stringification, `__FILE__`/`__LINE__`, `undef`, nested conditional compilation, logical `` `line`` source remapping, legal `` `timescale``, `` `default_nettype``, reset/cell/keyword-version/unconnected-drive state, and ordered `file`/`source-set`/`combined` policies | Included units and macro-selected executable source enter the normal frontend; active `` `line`` mappings reach parser diagnostics, macro ancestry, DesignIR/SimIR debug points, report callbacks, and LLVM objects while physical ownership remains in analysis/native cache provenance; mappings reset for includes and compilation-unit roots; source-set/combined roots otherwise share macro, conditional, and parser directive state while retaining library ownership; scalar implicit nets and default port net types honor `` `default_nettype``; cell metadata and omitted-input pulls reach DesignIR/runtime; time directives and declarations scale exact delays and contribute to `auto` resolution; ordered snapshots participate in cache identity; `wand`/`triand` and `wor`/`trior` use native per-driver four-state resolution | Standardized pragma payload behavior remains incomplete; unsupported directives receive targeted errors |
| SystemC | C++ compatibility header, macro-exported typed factories, HDL-backed module proxies, append-only versioned plug-in ABI, and peer mixed-language hierarchy | Common signals/ports/exports/events/channels, native children, same-path HDL proxy modules with ordinary port binding, ordered lifecycle callbacks, statically and dynamically sensitive `SC_METHOD`, and Boost.Context-backed `SC_THREAD`/`SC_CTHREAD` time/event/list/timeout/static waits execute on the deterministic common kernel | Executable behavior or nested objects inside HDL proxies, arbitrary custom-interface values/binding/updates, dynamic processes, thread reset/kill, TLM/AMS/CCI, and Accellera ABI compatibility remain unsupported |

Batch 162 UVM closure update: the exact unmodified UVM 1.2 and UVM 2020-3.1
package/macro entry points preprocess and analyze through the ordinary
SystemVerilog-2017 frontend under a governed external-source contract. The
executable foundation covers `uvm_object` and `uvm_component` construction,
identity/hierarchy, object utility registries, factory type/instance overrides,
typed resources and `uvm_config_db`, recognized command-line settings,
multiple simulation roots/contexts, and report objects, handler/server routing,
catchers, accounting, MCD/file sinks, and bounded formatting. Exact common,
runtime, and custom phase/domain graphs now execute function/task callbacks,
jumps, synchronization, process cancellation, objections, drains, ready-to-end
quiescence, and deterministic race/deadlock handling. Typed TLM1 ports/exports/
implementations, FIFOs, transport and analysis plus TLM2 sockets, generic
payloads, DMI, blocking/debug/nonblocking transport, activity/debug observers,
DPI/VPI snapshots, portable UVM checkpoints, sequence items/sequencers, every
arbitration mode, locks/responses, driver handshakes, virtual sequences,
agents/monitors/scoreboards, callbacks/transactions, and the register model are
executable. Register support includes value policies, memories, hierarchical
and multiple maps, all endian modes, byte enables, adapters/predictors,
frontdoor and VPI/VHPI backdoor access, standard sequences, callbacks, and
coverage. Object policies, packing/recording, synchronization, command-line,
test-runner, tracing, project core/flow/register environments, and portable
release/source provenance are also executable. The exact aggregate example
agrees through interpreter, LLVM O0/O2, and debug engines with VCD/FST,
cold/warm caches, portable object/design artifacts, relocation, and
deterministic transcripts. All services are simulation-owned and resource-
bounded. The locked supported boundary is executable through an 18-row
conformance inventory and a 21-row two-release closure matrix with zero
unresolved supported gaps; this remains a bounded support claim, not exhaustive
UVM conformance. See [`systemverilog-uvm.md`](systemverilog-uvm.md) for exact
support, [`uvm-tutorial.md`](uvm-tutorial.md) for producer-independent usage,
and [`uvm-closure-audit.md`](uvm-closure-audit.md) for evidence.

UDP status update: Verilog-2005/SystemVerilog combinational and sequential
user-defined primitives now retain ordered scalar terminals, exact level/edge
and state tables, optional initial output, static instance arrays, and
one/two/three-value propagation delays. They resolve as logical-library
candidates distinct from modules and built-in gates, execute through ordinary
four-state SimIR plus the common inertial scheduler, and preserve normalized
table identity through `.fsimobj`, `.fsimdesign`, mapped `.fsimlib`, LLVM
cache, debugger, callbacks, and VCD. UDP terminals are scalar and positional;
specify timing is implemented by the common module-path and timing-check model
described below. SDF annotation remains separate work.
Table and instance-array geometry use 256 MiB owning-storage budgets derived
from materialized records rather than IEEE language-count ceilings.

Specify timing status update: module-owned specify HIR retains declaration-
ordered scalar and mintypmax `specparam` values, `PATHPULSE` limits, parallel
and full paths, `if`/`ifnone`, source edges, polarity and destination-data
transforms, and all one/two/three/six/twelve transition-delay forms. The common
scheduler executes inertial path routing plus onevent/ondetect and cancelled-
pulse publication. All twelve Verilog timing checks retain event conditions,
signed compound windows, delayed reference/data signals, optional static
flags, notifier updates, and persistent per-instance history. The same
normalized state survives `.fsimobj`, `.fsimdesign`, mapped `.fsimlib`, native
cache, interpreter/LLVM O0/O2, debugger, callbacks, and VCD paths. SDF parsing
and annotation remain assigned to their dedicated v2 batch.

Verilog-2005 strength execution covers explicit zero/one drive pairs, pull and
supply sources, implicit `tri0`/`tri1` pulls, strength-qualified gates and UDP
outputs, MOS/resistive-MOS devices, passive `tran`/`rtran` and conditional
transmission networks, and `trireg` charge retention/decay. Resolution is
four-state and per bit; connected-net traversal is cycle safe and vector
controls select conductance per lane. Strength, topology, charge, and decay
metadata survive portable/object/design/library artifacts and interpreter/LLVM
O0/O2 cold/warm/edit cache paths.

Batch 163 Change 2 closes the IEEE 1076-2008 lexical and design-unit
surface. Basic identifiers canonicalize case while rejecting leading,
adjacent, and trailing underscores; extended identifiers retain case and
decode doubled backslashes. Character and doubled-quote string literals, all
simple/compound/matching/box/external-name delimiters, line comments, and
nested delimited comments are retained with line-oriented recovery. Bit-string
literals accept B/O/X/D, UB/UO/UX, and SB/SO/SX spellings, zero or positive
host-addressable widths, underscores between value characters, nine-state
replication, null values, arbitrary-precision decimal conversion, and only
lossless unsigned or signed adjustment. Unsized values preserve their complete
source-determined expansion; sized values retain exactly the requested suffix
while checking every discarded character, so literal legality is independent
of host-word width. Comment nesting above 64 produces one cataloged resource
diagnostic. Library, use, and context clauses require their exact selected-name
shape; trailing clauses cannot disappear at end of file. Primary-unit and
architecture/package-body secondary identities are library-scoped,
deterministically ordered, and diagnosed independently of the legacy generic
duplicate-unit error. Project manifests select only the implemented `2008` or
`08` VHDL mode; older and post-2008 modes are rejected rather than silently
reinterpreted.

Batch 163 Change 3 declaration work is complete. An incomplete type may
precede an access type that designates it and is replaced by its full declaration
in the same declarative region; duplicate and never-completed markers fail
exactly. Record declarations admit integer members and recursively named access/
composite members without pretending every record is already a flat packed
value. User-defined attribute declarations/specifications and group templates/
instances are retained as typed, source-spanned, source-ordered frontend
metadata with canonical target/class/value and template/entry ownership.
Deferred package constants may use arbitrary constrained subtypes, require a
structurally conforming full declaration in the package body, and merge that
initializer into the effective declaration. Attributes and groups propagate
through function, procedure, process, block, and generate declarative regions
and publish explicit semantic-HIR profiles. Portable artifacts retain deferred/
completion provenance at owning-unit schema 15, with exact diagnostics for
missing, mismatched, and illegal completions.

Batch 163 Change 5 sequential closure is complete. Sequential VHDL-2008
signal assignments accept effective-value force and release in default or
explicit `in` mode for whole packed signals, static slices, and supported
indices. Force overrides intervening ordinary driver updates until release and
then reveals the current driven value; interpreter and compiled O0/O2 execution
agree. Driving-value `out` mode targets only the current process driver with a
per-driver force mask before resolution, so other drivers continue to
participate; interpreter and native cold/warm compiled execution agree on the
resolved `0` to `X` to `0` transition across force, competing drive, and
release.

Batch 163 Change 6 concurrent/process closure is complete. Sensitized and
all-sensitive processes retain the existing direct/transitive wait legality;
ordinary and postponed processes, concurrent assertions, and concurrent
procedure calls now carry distinct phase ownership through frontend, semantic
HIR, SimIR, interpreter, and compiled execution. Postponed work observes the
settled value after active/update publication. Boolean-guarded blocks retain
one stable driver owner per guarded simple, conditional, or selected assignment.
Disconnection specifications in architecture, block, and generate declarative
regions select explicit signals, all matching signals, or matching `others`
not explicitly named elsewhere; their type mark and independent delay remain
owned and diagnosed. A false guard schedules resolution-neutral disconnection
after that specification delay, reactivation cancels the pending transaction,
and a later false transition restarts it. Multiple drivers resolve before and
after disconnection, interpreter destruction drops pending work without
cross-instance leakage, and a configured delta-cycle ceiling bounds a
nonconverging concurrent assignment. Guarded disconnection remains restricted
to the supported nine-state resolved target family; arbitrary user-defined
resolution-function invocation is outside this bounded execution slice.
This status supersedes the compact table's older statement that disconnection
specifications themselves were unimplemented.

Batch 163 Change 8 access/file/protected status is complete. Each VHDL access
type uses a process-owned associative live-object heap and a separate
append-only issued-identity ledger. Allocation returns a nonzero 32-bit handle
that is never reused during the simulation; deallocation removes the live
designated value and nulls the owning variable, and deallocating null is a
no-op. Null, stale, or foreign-owner dereference and assignment reject before
accessing storage. Live-object and lifetime counts are bounded by explicit
representation-derived container ceilings rather than an arbitrary language
width limit.

VHDL direct and TextIO file operations share the manifest-confined SimIR file
service. Read/write/append modes, lookahead `endfile`, direct integer and line
read/write, status opens, close, file-formal aliasing, and early-return or
simulation finalization agree across interpreter and compiled engines. File
handles are process-owned, never reused after close, and have a documented
4,096-open lifetime ceiling; null, unknown, stale, closed, cross-owner, and
wrong-mode use rejects deterministically.

Shared variables remain restricted to protected types. A protected method is a
wait-free source-ordered scheduler segment, so two processes cannot interleave
private-member updates. Active recursion/reentry, nested protected procedure
calls, and waits reject explicitly instead of blocking. An ordinary pure VHDL
function cannot call an impure protected function. Protected private members
accept every positive width representable in the container metadata; focused
execution covers a 137-bit member, deterministic two-process updates, and fresh
simulation defaults. Protected methods still use the bounded scalar callable
profile documented below; supporting suspending or recursively reentrant
protected execution would require a different scheduler contract and remains
outside this profile.

Batch 163 Change 9 establishes the embedded PSL source and ownership boundary.
In VHDL-2008 mode, case-insensitive `-- psl` comments retain an explicit lexical
marker while ordinary comments remain trivia. Native or comment-embedded
`vunit`, `vprop`, and `vmode` library units preserve their VHDL target, source
context, and unit kind. Entity, architecture, package, and verification-unit
declarative regions own default-clock, Boolean, sequence, property, and endpoint
declarations with ordered formal profiles and body tokens; entity/architecture
statement regions and verification units own labeled `assert`, `assume`,
`restrict`, and `cover` directives. The project analyzer publishes those records
in the owning VHDL semantic HIR without reparsing source text. Duplicate clocks,
declarations, formals, or labels; malformed bodies/targets/delimiters; illegal
placement; and unsupported verification-unit items receive the cataloged
`FSIM-VHDL-PSL-001` through `009` diagnostics. Temporal typing, clock inference,
execution, outcomes, and coverage remain Changes 10 through 12 rather than being
implied by this parse-level support.

Batch 163 Change 10 completes PSL temporal analysis. Visible scalar VHDL
Boolean/bit/logic objects are sampled through the owning or targeted entity and
architecture. Local, inherited, referenced, and explicit clocks are retained
with canonical edge identity; equivalent `rising_edge(clk)` and
`clk'event and clk = '1'` spellings agree, incompatible clocks reject, unknown
clock values produce no edge, and sampled unknowns use an explicit false policy.
Boolean, sequence, property, endpoint, and static-integer-formal classes are
distinct. Sequence concatenation/fusion and consecutive/nonconsecutive/goto
repetition, overlapped/nonoverlapped suffix implication, next/prev/eventually/
always, until/before/within, endpoints, and static ranges retain typed semantic
HIR. Literal bounds normalize numerically; typed integer formals retain symbolic
identity and any static default for later specialization. `FSIM-VHDL-PSL-010`
through `019` cover missing/invalid clocks, invalid sampled objects, temporal
type errors, nonstatic/malformed ranges, cross-clock references, endpoint and
name failures, malformed overrides, and reference cycles.

Batch 163 Change 11 executes that temporal HIR at the stable post-update sampling
boundary. Boolean default clocks and canonical rising/falling forms share clocked
history; unknown clock values create no edge. Source-ordered overlapping attempts
execute concatenation/fusion, consecutive/nonconsecutive/goto repetition,
declaration actual/default specialization, `within`, suffix implication,
`next`/`next_a`/`next_e`, `prev`, bounded or unbounded `eventually`, `always`,
`until`, `before`, strong/weak completion, and synchronous/asynchronous aborts.
The `[=]` form admits its trailing non-match span while `[->]` ends on the selected
occurrence. Interpreter, debug, LLVM O0/O2, and cold/warm cache paths retain exact
attempt identity and pass/failure/vacuous/aborted outcomes. Monitor, shared-
history, active/lifetime-attempt, evaluation, inner temporal-step, nesting, and
conservatively accounted 512-MiB owned-storage ceilings prevent unbounded runtime
growth.

Batch 163 Change 12 publishes every assert, assume, restrict, and cover attempt
with its directive kind, unit/instance identity, semantic source-span identity,
source-order slot, clock/sample/time/delta coordinates, and pass/failure/vacuous/
aborted outcome. Public trace and per-directive coverage views remain stable
across interpreter, debug, LLVM O0/O2, and cold/warm cache. Dedicated PSL and
common assertion callbacks run in deterministic completion order and contain
observer exceptions after coverage commit. Assert/assume failures use error
report routing, restrict failures use warning routing, and cover failures remain
coverage-only. The bounded attempt trace is retained directly rather than copied
into a second unbounded event history.

Batch 163 Change 13 integrates this execution with the existing IEEE and VITAL
profiles. Resolved `std_logic` clocks/predicates sample through the common packed
runtime value, while arbitrary-width `numeric_std`/fixed vectors, file objects,
protected objects, and physical values keep their normal VHDL semantics and are
not implicitly coerced into PSL Booleans. Signals produced by existing
`VitalPathDelay` calls are visible at the same stable sampling boundary. Portable
schema-17 library units preserve parsed PSL clocks, declarations, formals,
defaults, properties, directives, tokens, and source spans without source
reparse. SDF ownership remains explicitly deferred to Batch 170.

Batch 163 Change 14 makes PSL execution occurrence-aware across multiple and
mixed-language roots. Explicit `entity(architecture)` targets retain the named
architecture in `DesignIR`; configured root order controls deterministic monitor
and callback order; and clock/sample lookup is qualified by the exact VHDL
occurrence path. Duplicate VHDL roots and adjacent SystemVerilog or SystemC
roots may therefore reuse local signal names without ambiguity. A governed
SV-to-VHDL-to-SystemC graph proves converted port values, resolved SystemC
updates, and PSL sampling at the common stable-delta boundary across interpreter,
debug, LLVM O0/O2, cold/warm cache, and source edits. Simultaneous root clocks
share one immutable retained value snapshot, keeping history storage linear
under the existing explicit resource ceilings.

Batch 163 Change 15 exposes the resulting VHDL state through a bounded public
snapshot, generation-checked simulation-owned VHPI handles, and the debugger's
`vhdl summary|scopes|objects|processes|psl|all` views. Ordered occurrence scopes,
file/access/protected/physical declarations, aliases and external-name-derived
signals, live values, driver counts, active/postponed processes, source spans,
PSL attempts, named outcomes, and coverage counters remain stable across
interpreter, debug, LLVM O0/O2, and cold/warm cache. Existing VCD signal tracing
uses the same occurrence paths. Snapshot record/payload/text ceilings reject
transactionally, and foreign, released, or generation-stale VHPI handles cannot
be reused across simulations.

Batch 163 Change 16 makes that state durable. A versioned `FSIMVHIR` design
payload preserves the complete owning VHDL declaration/type/expression/process
HIR plus analyzed PSL clocks, declarations, operators, directives, and source
identity. The loader admits it only after schema, checksum, trailing-byte,
enum, unique-ID, semantic-ID-range, DesignIR, and runtime-projection checks all
succeed. Owning-unit schema 17 carries the frontend form through `.fsimobj` and
relocated `.fsimlib`; relocated `.fsimdesign` restores the semantic HIR without
the producer source. The IEEE standard-library identity advances to
`ieee-1076-2019-16a01232-vhdl-psl-wide-v2` and participates in whole-design and
per-specialization native keys. Direct, mapped-library, and design-artifact
execution reproduce the same PSL attempts, coverage, debugger snapshot, and
VCD across interpreter and LLVM O2 cold/warm runs. Portable VHPI checkpoint
restore verifies the artifact/cache identity and remaps stable object names to
fresh generation-qualified handles rather than serializing host addresses.

Batch 163 Changes 17-18 close and govern that boundary. The clause inventory
contains 29 supported VHDL-2008/embedded-PSL rows, zero unresolved active rows,
and four exact deferrals, with 87 positive/negative/execution witnesses. The
44-row release matrix requires 17 direct/interpreter/LLVM/cache/debug/trace/
artifact/relocation/replay/root/mixed stages and consolidates malformed, race,
cancellation, nonconvergence, resource, platform, installed-public, provenance,
and reviewed-complexity evidence. The runner has a 6 GiB process address-space
ceiling and completed at 195,272 KiB peak RSS. See the public
[`vhdl-psl.md`](vhdl-psl.md) support boundary,
[`vhdl-psl-tutorial.md`](vhdl-psl-tutorial.md) usage flow, and
[`vhdl-psl-closure-audit.md`](vhdl-psl-closure-audit.md) exact evidence.

Batch 118 type status update: VHDL-2008 access declarations resolve one
concrete bounded designated subtype and use opaque 32-bit nullable handles,
process-owned allocation storage, checked dereferences, and same-nominal
equality/copy/callable transfer. Protected declarations and bodies require
exact public-profile conformance; architecture shared objects construct source-
ordered private members. Bounded physical types use a
signed 32-bit primary-unit representation with positive declaration-ordered
secondary scales, static literal folding, checked arithmetic/comparison,
explicit conversion, range and overflow failures, and same-nominal legality.
The three families retain hierarchy, debugger, specialization, and cache
metadata and agree across interpreter and LLVM O0/O2. Access-to-protected or
nine-state designated objects, suspending or recursively reentrant protected
methods, broader protected method profiles, and physical values outside signed
32-bit ticks remain outside this bounded slice. The Change 8 status above
supersedes this historical paragraph's former deallocation/lifetime limits.

Batch 116 layout status update: VHDL array declarations retain
every ordered `integer`/`natural`/`positive` index subtype and constraint,
including mixed constrained/unconstrained dimensions, plus the complete direct
or named composite element type. Named subtype indications preserve all
multidimensional constraints on objects and ports. Nested aggregates,
multi-index names, subarray-slice operands, and array-typed function/procedure
boundaries remain source-spanned in typed HIR. Elaboration now folds every
dimension into an exact source-direction range, preserves null dimensions,
retains the complete nominal element subtype, and computes rightmost-fastest
packed-bit strides plus a deterministic total width for scalar, vector,
record, and nested concrete array elements. Rank mismatches receive
`FSIM-ELAB-VHARRAY-008`; noninteger index subtypes remain outside the retained
subset. Aggregate, selection, and general execution claims remain limited to
the completed one-dimensional scalar-element path until the later Batch 116
tasks close them.

Batch 116 aggregate status update: contextual aggregates now recurse across
concrete multidimensional subarrays, nested named arrays, and nominal record
elements. Each source dimension accepts positional, locally static discrete or
directed-range, choice-list, and final `others` associations with exact
direction-aware coverage and overlap checks. The same lowering path is used in
concurrent assignments, conditional alternatives, and process-local
initializers, with interpreter and LLVM O0/O2 parity. General multidimensional
selection and target syntax remains deferred to the next Batch 116 task.

Batch 114 generated-type status update: the VHDL unit row's earlier
generate-local subtype and generated-type exclusions are superseded. Selected
`if`/`else`, iterative, and labeled `case` alternatives retain bounded array,
enumeration, record, and subtype declarations before `begin`. Physical-source
ordering rejects forward type visibility; selected constraints fold prior local
constants and the concrete loop index. Realized declarations and their object
types retain exact source provenance plus branch- or iteration-qualified
nominal identity. Generated declarative parts also retain bounded VHDL function
and procedure declarations/bodies, local overloads, and value-generic
subprogram templates plus `is new` instances. Calls execute under scoped
identity with declaration-order visibility, exact generic bindings, and
cold/warm/edit cache provenance. Generated local-package instances are
materialized after branch selection, including iteration-dependent maps,
selected constants/types/callables, nested scopes, and exact package/cache
identity. Generated declarations also retain explicit typed signal aliases;
variable, file, attribute, use, group, and disconnect items remain targeted
unsupported forms. Architecture, process, ordinary-subprogram, and instantiated
generic-subprogram regions now retain bounded constants, types/subtypes,
signals or variables, explicit typed object aliases, local generic-package
instances, and nested non-suspending callables. Local constants and types use
declaration-order specialization, generic-template locals defer until their own
actuals bind, and nested callable capture is bounded to locally static outer
constants plus qualified local packages. Runtime outer-variable capture,
implicit-subtype aliases, file objects, attributes, use clauses, and groups
remain outside this local-region slice and receive deterministic diagnostics.
Selection-generate elaboration now also resolves locally static integer and
retained enumeration selectors across identifier and character literals,
static constants, grouped choices, ascending/descending and null ranges, and
`others`. Alternative labels remain canonical hierarchy segments, and
overlapping intervals, duplicate defaults, unknown literals, and nominally
mismatched enumeration choices receive deterministic generate diagnostics.

Batch 107 SystemVerilog unit-status update: the Verilog/SV unit row's earlier
interface and package-export exclusions are superseded. Bounded parameterized
interfaces now retain packed members, processes, continuous assignments,
functions, tasks, and one-dimensional static instance arrays. Explicit
interface/modport ports accept whole-interface or statically indexed actuals
through named, positional, nested, and generated hierarchy. Modports retain
checked input/output/inout/ref members plus function/task import/export
entries; input views are read-only, writable views use path-aware driver
ownership, and imported callables execute through specialized dotted views.
Package `export` supports selective, `package::*`, and `*::*` re-export of
explicitly imported constants, packed types, functions, and tasks with
transitive visibility, collision checks, cycle rejection, and source/cache
provenance. Dynamic interface arrays and general interface-type expressions
remain outside this bounded slice. Batch 153 supersedes the earlier exclusions
for clocking-block modport entries, bounded static interface arrays, interface
classes, and virtual interfaces.

Batch 108 preprocessing/generate status update: the Verilog/SV unit row's
earlier exclusions for genvar-dependent typed constants and generated type
declarations are superseded. Full directive-argument include expansion,
`` `undefineall``, checked macro redefinition, include-local conditional
frames, deterministic unlabeled `genblkN` names, and declaration-ordered
generated localparams now specialize packed ranges, local typedefs, objects,
function/task profiles, delays, instance overrides, and connections. Exact
physical/logical source ancestry and include snapshots participate in
preprocessor-v5 and native-schema-60 identity. Implementation-defined pragma
payload semantics, dynamic generate selection, and aggregate or
multidimensional generated types remain outside this bounded slice.

Batch 109 aggregate/multidimensional status update: the Verilog/SV unit and
expression rows' earlier exclusions for nested aggregates, nominal legality,
and all multidimensional unpacked arrays are superseded. Named packed structs,
unions, and enums retain recursive members, enum literals, declaration order,
layout, state domain, source provenance, and nominal identity through aliases
and type parameters. Bounded unpacked structs accept scalar, enum, and nested
packed or unpacked-struct members. Recursive positional, member/integral-keyed,
and default patterns initialize aggregates and ranked arrays atomically;
packed-union patterns select exactly one member. Visible named or bounded
builtin casts preserve the target type, and distinct nominal aggregate
assignments or equality comparisons require a matching explicit cast where
supported.

Static unpacked arrays retain any declaration-ordered count of locally constant
dimensions and
materialize within a 256 MiB per-container owning-storage budget. Full-rank
constant or runtime signed-32 indices
use direction-aware row-major flattening with per-dimension checks. Dimension,
bound, size, increment, and bit queries, whole-value copies, generated
declarations, same-language exact-rank ports, automatic function/task
boundaries, debugger reads, callbacks, VCD, interpreter, and LLVM O0/O2 are
covered. Native schema 61 and container semantic revision 25 preserve ordered
dimensions and nominal identities. Packed structs, equal-width ordinary
packed unions, unequal-payload tagged unions, recursive aggregate members and
arbitrary-width packed values retain exact layouts. Multidimensional subarray
slices, cross-language aggregate/container boundaries, and general aggregate
streaming remain unsupported.

SystemVerilog expression-sizing status update: packed values retain
source-spanned resolved width, signedness, self- versus context-determined
sizing, and two-/four-state domain metadata without a language-level 64-bit
cap. Sized,
unsized, unbased-unsized, unary, arithmetic, bitwise, comparison, shift,
power, conditional, concatenation, and replication values apply the supported
SystemVerilog extension/truncation rules at assignments, arguments, returns,
conditions, and read selections. Runtime `&&`, `||`, and `?:` use explicit
branch-directed evaluation: definite controlling values skip unneeded
time-free function calls, while an X/Z conditional evaluates both alternatives
once and performs the required bit merge. Bounded functions may therefore
write nonlocal variables for observable time-free side effects; input-formal,
nonblocking, and timed writes remain rejected. Constant conditionals likewise
evaluate only the selected alternative while retaining the common alternative
profile, so an unselected invalid operation cannot perturb the result.

Runtime-base packed `base +: width` and `base -: width` reads now support a
positive locally constant width, exact ascending/descending declared-range
mapping, per-bit X filling for partial four-state out-of-range selections,
zero filling for two-state values, and all-X/zero results for an unknown base.
Dynamic procedural part-select targets share the same captured selection for
blocking, delayed, event-controlled, and nonblocking updates. Fixed-width
packed integral streaming concatenation supports `{<<{...}}`,
`{>>{...}}`, positive constant slice sizes, nested ordinary concatenations,
constant folding, exact final partial chunks, and arbitrary packed result
widths. Dynamic stream sizes and aggregate/container streams remain deferred
to their container closure. This update supersedes the compact
table's older statements that all dynamic part-selects, streaming
concatenations, vector conditional truth, observable expression side effects,
and supported scalar context sizing were pending.

SystemVerilog procedural-lvalue status update: whole signals and locals,
packed members, static bit/part selections, chained static packed selections,
runtime bit selections, and runtime-base indexed part selections now share one
checked target capture. Compound assignments accept the supported delay and
event controls; expression-form and standalone prefix/postfix `++`/`--`
preserve new/old result ordering. Runtime-base writes update only representable
bits, perform no write for unknown or wholly out-of-range bases, and preserve
their captured target through delay, event, and NBA scheduling. Procedural
force/release supports whole packed signals, packed members, and static
bit/part selections with per-bit masks while underlying drivers continue.
Force/release of automatic locals or runtime-selected targets, a further
selection after a runtime target, and non-integral container targets remain
unsupported. Packed lvalues preserve every word within the explicit SimIR
offset/width representation. This update supersedes
the compact table and expression-sizing note where they defer dynamic
procedural part targets, chained packed targets, timed compound assignments,
expression updates, or all procedural force.

SystemVerilog constant-expression status update: supported integral parameter
expressions use an arbitrary-width typed semantic value rather than host-C++
promotion rules. It retains every packed word, signedness, X/Z masks, unsized status, and
source span for sized, unsized decimal, unsized based, and unbased unsized
literals. The bounded operator set covers unary/reduction operators,
arithmetic, bitwise/logical operators, equality and case equality, relations,
shifts, power, conditional expressions, concatenation, replication,
`$signed`, `$unsigned`, `$isunknown`, `$clog2`, and nominal-aware `$typename`.
Defaults, localparams,
named/positional overrides, non-iterative generated constants, packed ranges,
and conditional/case generate choices share this evaluator. Four-state
values remain legal in four-state parameter types and are rejected when a
two-state conversion would lose information. The canonical `svconst-v1`
identity prevents equal display strings with different widths or signedness
from sharing native objects. Checked integer-only consumers diagnose values
outside their explicit host representation; the constant evaluator itself is
limited only by the governed work and storage policy.

SystemVerilog time-format status update: `$timeformat` installs one mutable
simulation-global units, decimal precision, suffix, and minimum-field-width
profile. Terminal, file, monitor, and mutable-string `%t` conversions use the
same exact integer scaler, including project resolutions whose magnitude is
not a power of ten. A bare `%t` uses the profile width; `%0t` suppresses that
padding, and an explicit field width replaces it. Precision and suffix growth
are bounded by the common output-string storage policy rather than by an HDL
time or bit-width restriction.

`$printtimescale` uses the elaborated DesignIR occurrence and retained
SystemVerilog compilation context to report the current or explicitly selected
instance's exact time unit and precision. Relative and `$root` hierarchy names
resolve against stable instance paths, and interpreter plus compiled execution
share the same application service rather than embedding hierarchy pointers in
SimIR.

`$time`, `$stime` and `$realtime` accept both bare and empty-parentheses
forms. Each query carries the executing specialization's retained time unit
and precision through portable SimIR, then converts the common scheduler tick
with the same checked scalar-time service in interpreter and compiled modes.

`$get_coverage()` returns a real percentage from 0 through 100 using the live,
goal-adjusted coverage of every materialized covergroup type and each type's
`type_option.weight`. `$get_inst_coverage()` instead contributes every
materialized covergroup instance using the declaration's effective instance
`option.weight`. Empty or zero-weight contributors are excluded from their
respective denominators. Both queries use one typed portable SimIR service, so
runtime-state round trips and interpreter or LLVM O0/O2 execution share the
same simulation-owned coverage state.

`$set_coverage_db_name(filename)` selects a project-root-confined database
written when simulation finishes. `$load_coverage_db(filename)` reads the
current fsim coverage schema and transactionally accumulates matching bin and
cross counts by stable declaration, instance, and bin identity. A different
model, malformed/current-schema mismatch, absolute path, or escaping path is
rejected without partially changing live coverage. Sampling progress,
callbacks, and trace history remain run-local rather than being imported.

SystemVerilog membership-expression status update: packed integral
`lhs inside {value, [low:high], ...}` expressions preserve one source-spanned
left operand and an ordered nonempty value/range list. Operands use the common
SystemVerilog comparison width and signedness. The left operand is evaluated once; exact values
and inclusive ascending closed ranges are tested in source order, reversed
known ranges are empty, and a definite match skips all remaining members. X/Z
bits in a value member act as wildcards. An unmasked unknown left bit or an
unknown range comparison propagates X unless a later member definitely
matches. Constant folding and interpreter/LLVM O0/O2 execution use the same
arbitrary-width semantics. Variable-array sets, open ranges, and type/class
membership remain unsupported; case-inside statements are described below.

SystemVerilog case-inside status update: bounded scalar integral
`case (selector) inside` statements accept ordered alternatives containing
exact values, inclusive ascending `[low:high]` ranges, mixed comma-separated
choices, and one final `default`. Selector, values, and bounds require exact
common comparison sizing and signedness. The selector executes once; X/Z bits in a value choice
are wildcards, unknown comparisons fall through, a later definite match may
select its alternative, and otherwise default executes. Known reversed ranges
are empty, the first definite match skips all later choices and alternatives,
and constant-function plus interpreter/LLVM O0/O2 selection agree. Variable-
array/open/type/class sets remain unsupported.

SystemVerilog case-pattern status update: packed integral
`case (selector) matches` statements accept one constant, `.*`, `.binding`,
tagged-union, or positional/named packed-structure pattern per nondefault item,
with recursive nested patterns and optional `&&&` guards. Constant patterns
use common comparison sizing; bindings are visible only to the matching guard
and body. Guards execute only after their pattern matches. Tagged patterns
check the retained tag and selected payload, while structured positional
patterns cover every member and named patterns may select a subset without
mixing forms. The selector executes once, the first matching body or final
default executes in source order, and `unique`, `unique0`, and `priority`
retain their established alternative-level warning rules. Constant-function
scalar bindings, interpreter, LLVM O0/O2, reports, and cold/warm cache behavior
agree. Type/class patterns and comma-list patterns remain outside this slice.

SystemVerilog case-qualifier status update: `unique`, `unique0`, and `priority`
may qualify exact, `casez`, `casex`, bounded `case inside`, and bounded
`case matches` statements.
Matching is counted per alternative rather than per comma-separated choice.
`unique` warns for multiple matching alternatives and for no match without a
default; `unique0` warns only for multiple matches; `priority` warns only for
no match without a default. Unknown comparison results are nonmatches for
these checks. All alternatives are checked before the first matching body or
default executes, preserving ordinary source-ordered selection. Warnings use
source-aware common report callbacks and agree across constant-function
selection, interpreter, LLVM O0/O2, CLI rendering, cold/warm cache reuse, and
source edits. Qualifiers outside SystemVerilog, repeated qualifiers, and a
qualifier not followed by a case statement receive targeted diagnostics.

SystemVerilog type-parameter status update: module and package parameter
regions accept `parameter type` and `localparam type` declarations without a
host-word width limit.
Defaults and named/positional actuals may resolve supported integral builtins,
local typedefs, wildcard-imported types, and directly package-selected marks.
Identifier actuals remain tentative until matched to a type formal, while
unambiguous builtin data types retain explicit typed HIR. Per-specialization
aliases flow into dependent packed ports, signals, typedefs, value parameters,
localparams, and value-dependent default ranges; nested same-language
forwarding preserves declaration order and source provenance. Versioned
`sv-type-v3` identities include scalar family, enum values, packed dimensions,
recursive container elements, virtual-interface identity and class actuals in
native-object cache keys. Unpacked/interface/class/anonymous composite actuals,
generated type declarations, and mixed-language type-parameter transfer remain
unsupported.

SystemVerilog string-parameter status update: module and package parameter
regions accept immutable `parameter string` and `localparam string` values.
Source literals retain parser-decoded bytes, including embedded zero bytes;
bounded identifiers, concatenation, equality/inequality, and
integral-selected conditional expressions fold during specialization.
Defaults, named/positional overrides, package constants, non-iterative
generated constants, and nested same-language forwarding share the typed
string evaluator. Constant strings may select conditional/exact-case
generate alternatives and substitute into the supported `$display`, `$write`,
`$strobe`, severity-report, and immediate-assertion message positions.
Versioned `svstring-v1` identities retain exact byte length/content in
specialization and native-cache keys.

SystemVerilog runtime-string status update: bounded module variables,
automatic block/function/task locals, string function results and formals,
and task input/output/inout copy semantics execute through the reference
interpreter and native LLVM O0/O2. One strict UTF-8 service owns literals,
empty initialization, blocking value-copy assignment, concatenation,
equality/inequality, Unicode-scalar indexing/replacement/slicing, `len()`,
iteration, the standard comparison/case/substring and integer/real conversion
methods, and `%s` output. String locals survive suspending automatic tasks and
remain visible at debugger safe points; module strings support escaped `show`
and bounded `deposit`, while `force` is rejected. Values are resource-bounded,
and invalid UTF-8/scalars/indices or expanding operations that exceed a budget
leave the prior value unchanged. String operation/layout/provenance semantics
participate in versioned native-cache identity. Cross-language string
boundaries and string associative indices remain unsupported.

SystemVerilog container status update: one-dimensional integral dynamic arrays
`[]`, unbounded queues `[$]`, bounded queues `[$:N]`, integral-key
associative arrays, and locally constant static unpacked arrays `[left:right]`
execute as distinct module objects and automatic block/function/task values.
The bounded subset supports whole-value copy, element reads and writes, and
`size()`; dynamic, queue, and associative containers initialize empty and
support `delete()`, while fixed arrays materialize their declared range in
index order with bit-zero or four-state-X defaults.
Dynamic arrays add `new[size]`; queues add `push_front`, `push_back`,
`pop_front`, and `pop_back`; associative arrays add `exists(index)`,
`delete(index)`, and `first`/`last`/`next`/`prev` traversal. Values preserve
exact element width, signedness, and two-/four-state domain. Associative keys
add the same exact type metadata, require known values, and remain in canonical
numeric order; a missing-key read returns the element type's zero default
without inserting. A full bounded queue discards its back element after
insertion. Containers have no 4,096-element language cap. Materialized owning
storage is guarded at 256 MiB per container using the actual `PackedLogic4`
representation; associative arrays account for both keys and values. A
source-declared bounded queue retains its independent declared capacity.
Direct supported container objects also admit `$left`, `$right`, `$low`,
`$high`, `$increment`, `$size`, `$bits`, `$dimensions`, and
`$unpacked_dimensions`. Static-array bounds, direction, size, and bit count
fold from the specialized type. Dynamic arrays and queues derive bounds,
size, and bit count from their current value; an empty object has right/high
`-1`. Associative arrays support entry-count `$size`, entry-width `$bits`, and
dimension counts but reject finite-bound queries. The optional dimension is
limited to the locally constant unpacked dimension `1`; type-only, indirect,
and multidimensional container forms remain outside this bounded subset.
Direct whole-container blocking assignments also accept apostrophe-brace
patterns. Static arrays accept either an exact positional count mapped from
the declared left bound toward the right bound, or exactly one
`default: value` plus zero or more locally constant integral `index: value`
members. Static keys convert to the signed 32-bit declared-index profile,
must remain in range and unique after conversion, and map correctly for
ascending or descending ranges. Unmentioned indices receive the converted
default value before explicit keys replace their slots. Dynamic arrays resize
to the positional count; queues append in source order and enforce their
optional bound. Associative patterns use locally constant unique integral keys
converted to the exact index profile. Construction occurs in a temporary typed
container before one whole-value copy, so empty patterns clear every
non-static kind and no destination is partially replaced. Positional mixing
with keyed/default members, defaults outside this static subset, type-keyed or
nested patterns, indirect targets, and nonintegral elements remain
unsupported.
Direct one-dimensional integral static arrays also support blocking
`[left:right]` slice assignment when both locally constant known bounds form
an in-range subrange in the array's declared direction. A slice retains its
selected declared range and exact element profile in a process-local typed
value. Slice-to-whole, whole-to-slice, and slice-to-slice assignment require
equal element counts and identical width, signedness, and two-/four-state
domains; they map elements by ordinal left-to-right position even when source
and destination indices or directions differ. The complete RHS is
snapshotted before a selected destination is merged into one whole-array
replacement, so overlapping self-assignment and object/port writeback are
atomic and preserve X/Z state. Module objects, writable same-language static
ports through nested/generated hierarchy, automatic function values, and
inout task values across suspension execute in interpreter and LLVM O0/O2.
The same direct slices are read-only receivers for `$left`, `$right`, `$low`,
`$high`, `$increment`, `$size`, `$bits`, `$dimensions`,
`$unpacked_dimensions`, and `.size()`. They also support all five reductions
with implicit or named transformations, all four extrema/uniqueness locators,
and all six predicate locators. Methods consume a selected typed snapshot;
iterator `.index` values and index-valued results use the selected signed
declared indices. Module objects, input and writable static ports, hierarchy,
automatic functions, and suspended tasks agree in interpreter and LLVM O0/O2.
Direct compatible slices may also be passed by value to fixed static-array
function inputs and task input/output/inout formals. Copy-in adapts equal-count
ranges ordinally into the formal's declared range. Task output and inout
copy-out occurs atomically only after normal or valueless-early return,
including after suspension, while every output formal begins each call with
its exact typed default. Exact width, signedness, state domain, and X/Z bits
remain unchanged through module objects, writable ports, and nested/generated
hierarchy.
Direct writable slices also accept `reverse()`, `sort()`, and `rsort()`, with
the same optional implicit or named `with` keys as whole-container ordering.
Only the selected ordinal range is reordered. Keys see the selected signed
declared `.index`, are computed once from the selected snapshot, and retain
equal-key order. The finished selected value is merged into one whole-array
replacement, leaving surrounding elements unchanged and preserving exact X/Z
state through module objects, writable ports, generated hierarchy, and tasks
after suspension.
Direct static-array selections may use locally constant `[left:right]`,
`base +: width`, or `base -: width` syntax. Indexed selections require a known
signed-32 base and positive width; their checked numeric interval is oriented
to the receiver's declared direction. Both indexed operators therefore work
with ascending or descending receivers when the computed interval is in
range. The normalized selected type supports the same assignment, query,
reduction, locator, ordering, fixed function/task actual, interpreter/LLVM,
debugger, and VCD behavior as the equivalent colon slice.
Direct named or positional same-language static-array module-port actuals may
use any of those locally constant slice forms. A child input sees a read-only
formal-typed ordinal view; output and inout writes merge one complete formal
value into one copied parent value and commit only the selected range.
Equal-count ranges may use different indices and directions, while width,
signedness, state domain, and X/Z bits remain exact. Recursive aliasing carries
the view through nested/generated hierarchy, and disjoint selected writers
coexist while overlaps retain deterministic rejection. Fixed and nonstatic
container-returning calls may be consumed directly by supported queries,
indexing, reductions, extrema, uniqueness, and predicate locators. Exactly
compatible fixed, dynamic, and queue results may also be conditional
alternatives. Known conditions copy one isolated result; X/Z conditions merge
equal-shape four-state elements bitwise, coerce unknown bits to zero for
two-state elements, and reset unequal nonstatic shapes to the empty value.
Exactly compatible fixed, dynamic, queue, and integral-key associative values
also support whole-container `==`, `!=`, `===`, and `!==`. Logical equality
returns false for a known size, key, or element mismatch and otherwise
propagates an unknown four-state result; two-state element profiles produce a
two-state result. Case equality compares X/Z planes exactly and always returns
a known bit. Each function-result or conditional operand is evaluated once in
lexical order. Associative conditional values and mutating methods on temporary
results remain unsupported. Schema 50 and container semantic revision 24 cover
result kinds, ranges/bounds, element/index profiles, consumer, conditional, and
comparison operations, specialization, and transitive source provenance;
normalized equivalent values share native cache identity without a public ABI
change.
Variable or unknown bounds or indexed widths, nonpositive widths, indirect
slice receivers, general expression port actuals, unrestricted
container-valued expressions, multidimensional and nonstatic-container slices, element
conversion, and cross-language slices remain unsupported.
Direct writable static arrays, dynamic arrays, queues, and bounded queues also
accept no-argument `reverse()` plus `sort()` and `rsort()` method statements
with an optional parenthesized `with` key.
Static values use declared left-to-right order and dynamic/queue values use
current index order. Sorting is stable for exact duplicates. Unsigned values
compare most-significant bit first with `0 < 1 < X < Z`; signed values use
`1 < 0 < X < Z` at the sign bit and the unsigned rank elsewhere, giving
ordinary two's-complement order for known values and a deterministic total
order for four-state values. A key binds implicit `item` or one named
iterator, exposes the original signed declared/current `.index`, and admits
the same bounded pure element/index comparisons, logical composition, local
constants, and one element-typed conditional selection as transformed
reductions. Every key is computed once before stable ascending/descending
sorting, so equal keys retain original order. Ordering supports writable
module objects, output/inout port aliases, and automatic task values across
suspension. No-argument `shuffle()` uses a deterministic per-process random
stream and the same interpreter/compiled Fisher-Yates implementation. Arbitrary
value arguments, keys on `reverse` or `shuffle`, associative arrays, indirect
or read-only receivers, arithmetic/calls/side effects in keys, and expression-
result use remain unsupported.
Direct nonassociative static arrays, dynamic arrays, queues, and bounded
queues also support `min()`, `max()`, `unique()`, and `unique_index()` with
an optional parenthesized `with` transformation when their result is assigned
to a compatible queue.
Value results preserve the exact element profile; index results use signed
two-state 32-bit elements. Empty sources produce empty results, extrema return
one first-occurring value, uniqueness preserves first occurrences by
four-state identity, and unique indices use signed declared static indices or
current dynamic/queue indices. Results remain bounded to the source-derived
owning-storage budget or destination queue capacity, and aliased queue
assignment evaluates the source
before replacement. A transformation binds implicit `item` or one named
iterator, exposes its original signed declared/current `.index`, and reuses
the bounded pure element/index graph: local constants, comparisons, logical
composition, and one exact element-typed conditional key selection. Keys are
computed once before selection. Extrema compare keys but return the first
original extremal element; uniqueness returns the first original element or
original signed index for each exact four-state key. Arithmetic or calls,
side effects, nonconstant operands, indirect index selection, mixed profiles,
multiple conditionals, associative locators, indirect receivers, and results
outside a compatible whole-queue assignment remain unsupported.
The same direct nonassociative containers support `find()`, `find_index()`,
`find_first()`, `find_first_index()`, `find_last()`, and
`find_last_index()` with one required `with` predicate. The bounded predicate
subset binds one integral `item` iterator and admits element-convertible
locally constant operands, equality/inequality, signedness-aware relations,
and logical `&&`, `||`, and `!`. Only an exact scalar one selects an element;
X/Z predicate results are false. Value methods return exact-element queues,
index methods return signed two-state 32-bit declared static or current
dynamic/queue indices, and first/last forms return at most one entry. The
common empty, destination-capacity, owning-storage, object/port/callable,
alias-safe, interpreter, and native-cache policies apply. Iterator indexing,
function calls, side effects, nonconstant external operands, case/wildcard
equality, arithmetic involving the iterator, associative receivers, and
general expression-result contexts remain unsupported. One optional named
predicate iterator and its direct signed 32-bit `.index` leaf are supported;
static arrays expose declared signed indices and dynamic arrays/queues expose
current zero-based indices.
The five exact-element reductions also accept one optional parenthesized
`with` transformation on nonassociative containers. The pure bounded form
binds implicit `item` or one named iterator and its direct signed 32-bit
`.index`, admits locally constant element alternatives, comparisons and
logical composition, and one conditional element selection for masking. The
binder is scoped to its transformation and does not affect semantic cache
identity. The graph is evaluated once per element in declared/current order
before the reduction; empty identities, four-state conditional merging,
receiver nonmutation, object/port/callable coherence, and interpreter/native
parity are preserved. Associative receivers, arithmetic or function calls
involving the iterator, side effects, multiple/nested conditionals, and
non-element transformation roots remain unsupported.
`$readmemb` and `$readmemh` load fixed arrays through the manifest-root file
service with optional start/finish indices, line/block comments, hexadecimal
`@` addresses, a 1 MiB input bound, and exact X/Z digit preservation.
Unaddressed data defaults to numerically increasing indices; explicit
start/finish magnitudes select ascending or descending progression.
Automatic values copy through nonrecursive calls, preserve copy isolation and
ordered task copy-out, survive suspended tasks, remain debugger-readable, and
execute identically through the interpreter and native LLVM O0/O2 callback
boundary. Wildcard/string/composite keys, multidimensional unpacked arrays,
dynamic static-array bounds, nonintegral/aggregate/string elements,
mixed-language transfer, and unrestricted allocation remain unsupported.

Batch 110 string/file/container/memory audit update: mutable byte strings now
cover deterministic standard methods, integer conversions, `$swrite`,
`$sformat`, and `$sformatf`; direct same-language input/output/inout string
ports alias one object through hierarchy with read-only input and writer
ownership checks. Manifest-confined files additionally cover character
pushback, formatted scans, binary `$fread`, positioned access, explicit/all-
file flush, and binary mode aliases. `$writememb` and `$writememh` provide the
checked inverse of the supported memory-load path.

The earlier integral-element-only container limits are superseded for bounded
named packed aggregate elements. Exact
arbitrary-width packed struct, union, and enum identity now follows static,
dynamic, queue, associative, and one-through-four-dimensional static
containers through nested patterns, element mutation, initialized dynamic
allocation, indexed queue insertion/deletion, full-rank indexing, generated
same-language ports, type parameters, automatic callables, debugger reads,
callbacks, and VCD-observed results. Distinct nominal element types remain
incompatible even when their layouts match. One-dimensional fixed arrays of
those packed words support `$fread`, `$readmemb`/`$readmemh`, and
`$writememb`/`$writememh`; multidimensional memory-file operands and string or
unpacked-aggregate elements remain checked exclusions. Native schema 75 and
container semantic revision 29 retain exact aggregate identity and the
representation-derived owning-storage policy,
construction/mutation operands, string-port aliases, dimensions, source, and
debug provenance. Unicode and real string conversions are now executable;
standard descriptor aliases, string-element associative indices, and
multidimensional memory-file operands remain explicitly deferred by the v1
matrix rather than silently accepted.

VHDL array-aggregate status update: constrained scalar-, vector-, record-,
enumeration-, and nested-array element types now accept contextually typed
positional, locally-static discrete/range/choice-list, and final `others`
associations at each bounded dimension. Choices follow each declared ascending
or descending ordinal range and may fold visible package constants or prior
generics. Nested aggregates preserve exact contextual shape and nominal record
identity. Qualified aggregate expressions and dynamically chosen associations
remain unsupported.

VHDL array-attribute status update: concrete user-array objects and visible
type/subtype marks support `left`, `right`, `low`, `high`, `length`, and
`ascending`, with optional dimension `1`. Sequential loops accept `range` and
`reverse_range` as complete discrete ranges and preserve declared direction,
including bounded `next` and `exit` behavior. These static scalar attributes
also participate in array indices, slice bounds, and aggregate choices.
Unconstrained marks, dimensions other than `1`, and scalar use of a range
attribute are diagnosed.

Batch 117 nested-composite status update supersedes the older coarse-table
limitations that describe records as non-nested or qualified/nested aggregate
expressions as unlowered. Bounded packed records now recurse through named
record, array, enumeration, vector, Boolean, bit, and logic members with exact
nominal identity, defaults, constraints, offsets, and checked acyclic width.
Qualified expressions and supported subtype conversions preserve exact
integer or nominal-composite contracts. Contextual record/array aggregates
accept element-name and discrete/range choice lists, qualification, and final
`others`; scalar/enumeration and multidimensional array attributes execute and
fold over exact declared subtypes and dimensions. Same-base composite
comparison, matching bit/logic equality, contextual concatenation,
assignment, conditional/case values, and conversions execute with recursive
profile checks. Bounded names may interleave record selections and array
indices/slices for reads and local/signal targets. Those nested composites
cross same-language hierarchy, generic-dependent, and callable boundaries
with recursive cache identity, exact aliases/copies, driver ownership,
sensitivity, scheduling, debugger/VCD, and provenance behavior. Integer,
access, protected, dynamically sized, and implicit cross-language composite
values remain outside this bounded contract.

VHDL interface-type-generic status update: entity generic clauses accept the
VHDL-2008 unclassified `type T` form mixed with existing value generics.
Named or positional actual subtype indications retain their type mark,
constraint direction, bound expressions, and source span. They resolve
builtin types, parent-local types/subtypes, direct package-selected types, and
a constrained actual passed through another type-generic hierarchy level.
Supported scalar, packed-vector, portable integer-range, bounded record,
nominal enumeration-range, and one-dimensional scalar-element array actuals
specialize dependent ports and internal signals per occurrence. Bounds may
use parent value generics, including nominally typed enumeration generics. A
packed formal constraint may also use a later value generic after the actual
base type is known. Null, out-of-base, re-constrained, wrong-kind, invisible,
value/type-mismatched, and unconstrained-object cases are diagnosed. Type
identity participates in native-cache keys, and interface type actuals cannot
cross a VHDL/SystemVerilog/SystemC boundary implicitly.
Bounded VHDL-2008 interface function generics now retain pure scalar
integer/Boolean/bit or visible scalar-subtype profiles, required, named, and
`<>` defaults, and named/positional function actuals. Unique conforming pure
local functions and directly visible package functions with matching bounded
package bodies bind per specialization, fold in constant/default expressions,
execute in concurrent and sequential expressions, and forward through a
nested generic entity. Function identity and declaration/body source
dependencies participate in specialization and native-cache keys.
Bounded VHDL-2008 interface procedure generics retain constant- or
variable-class scalar profiles with `in`, `out`, and `inout` modes, required,
named, and `<>` defaults, and named/positional procedure actuals and calls.
Unique conforming local or directly visible package procedures bind per
specialization, forward through nested entities, and execute with deterministic
input copy-in and ordered output/inout copy-out to signal or variable actuals.
Procedure frames retain debugger call points and live formals/locals, while
profile, body source, and package-body dependencies enter specialization and
native-cache identity.
Bounded VHDL-2008 interface package generics select same-language
entity/architecture-local package instances of a named generic package
template. Templates and instances may use the existing scalar value, type,
function, and procedure generic families with explicit maps, individual
`<>` defaults, or a whole `<>` map. Specialization materializes selected
constants, types, functions, and procedures beneath the formal prefix,
supports exact forwarding through a nested entity, and retains canonical
template/map identity plus transitive package-body source dependencies for
native-cache invalidation. General or nested generic package units,
generated/scoped package instances, overload sets, and mixed-language package
actuals remain unsupported.
Bounded VHDL-2008 generic function and procedure templates may appear in
package, entity, or architecture declarative regions and use the existing
scalar value, type, function, and procedure generic families. Directly visible
local or package templates instantiate in declaration order through explicit,
individual-`<>`, omitted-default, or whole-`<>` maps. Matching package
declarations and bodies specialize to independent callable functions or
time-free procedures; those instances may bind and forward as same-language
interface-subprogram actuals. The canonical instance identity retains the
template, map, callable profile, declaration/body sources, bound helper
subprograms, and transitive package provenance for selective native-cache
invalidation.
Bounded VHDL-2008 configuration declarations may be selected as a project top
and retain recursive configurations for existing labeled blocks and
statically selected one-dimensional for/if/case-generate occurrences.
Architecture declarative configuration specifications use the same binding
model. Explicit label lists, `all`, and `others` select only component-style
instances; direct entity instances and external manifest bindings remain
independent. `use entity library.entity(architecture)` and same-language
`use configuration library.name` bindings may compose named generic and port
maps through normalized component actuals. `use open` explicitly defers to the
bounded same-library default binding. The nearest matching nested rule
wins with enclosing-rule fallback, and a referenced configuration governs only
its child subtree. Version-2 recursive selection/reference identity, maps,
selected targets, and transitive physical sources participate in specialization
and native-cache provenance. Incremental configurations, dynamic or multi-index
generate specifications, general block-specification ranges, positional
binding-indication maps, complete default-binding rules, and mixed-language
configuration references remain unsupported. This update supersedes the
compact table's blanket statement that configurations are unavailable.
Failed recursive children are removed transactionally from dense runtime state,
name/path indexes, configuration/interface ownership, and boundary-driver
tracking before later siblings elaborate; the checkpoint stores sizes and
mutation identities rather than cloning the complete design.
Bounded VHDL-2008 component declarations retain their architecture, entity,
package, block, or selected-generate owner, lexical scope, declaration order,
optional end name, value/type/function/procedure/package generic profiles and
defaults, and scalar/vector, enumeration, named-subtype, non-nested record, or
one-dimensional scalar-element array port types, modes, and default metadata.
Component-style instances first select the nearest lexically visible
declarations, then directly visible package declarations. Component input
defaults and explicit `open` port actuals remain distinct HIR states until
that selection is complete.
Equally visible overloads are filtered by association shape, modes, types, and
specialization-dependent widths plus nominal composite identity, subtype
constraints, index direction, and specialized non-value generic profiles
without leaking declarations into sibling regions. Direct and selected package
type marks resolve in the declaration's own visibility context; a package
`use` does not re-export imported types.
Named and positional generic/port associations are normalized against the
selected declaration and mapped by formal position into a compatible
same-library entity; absent an explicit configuration, the latest analyzed
compatible architecture is selected. Architecture specifications and recursive
configuration rules retain precedence and apply to that selected profile.
Explicit, omitted-default, and box actuals are normalized across the existing
bounded interface type, pure scalar function, time-free procedure, and generic
package families. Omitted component generics materialize the component
declaration's default independently of the entity default. Dependent ports are
specialized before overload and target-profile matching, and configuration
maps compose through renamed non-value formals. Omitted or explicitly open
inputs materialize a statically foldable component default; omitted or open
output-family formals receive an owned disconnected child port and create no
parent alias or boundary driver. Supported defaults cover the existing scalar,
packed-vector, enumeration, named-subtype, non-nested-record, and one-
dimensional scalar-element-array types, including positional/named/`others`
aggregate forms and declaration-visible package or prior-generic constants.
Version-5 component profile/binding identity retains declaration region/scope/
order, owner and package sources, resolved nominal/subtype provenance,
canonical selected actual identities, normalized default/open states, mapped
target formals, configuration identity, profile, and target, so a callable/
package/type/default or visible-profile edit invalidates component consumers
without invalidating unrelated direct-entity children. Whole-signal composite
ports execute through the existing same-language nominal boundary model.
Bounded VHDL input expressions materialize an owned formal signal and, when the
actual is not static, a reactive child-local driver over aliased parent inputs.
A locally static one-dimensional array-element output actual materializes an
owned formal and a scheduler-visible slice bridge; separate children may drive
the same resolved element while disjoint elements retain independent driver
regions. Dynamic output indices and selected output slices remain unsupported.
New generic families, nested package/template forms, nested or otherwise
unsupported composite interfaces, general aggregate port actuals,
dynamic or mixed-language defaults, entity-port defaults, incremental
configurations, complete library analysis-order semantics, and general
overload resolution remain unsupported. Required unassociated direct-entity
inputs are rejected; component defaults never implicitly become entity
defaults.
Operator-symbol designators, unconstrained/composite parameters or results,
generated or nested generic subprogram templates/instances, general overload
sets, suspending generic procedures, interface-package formals nested inside a
generic subprogram, and mixed-language subprogram actuals remain unsupported.
Type-generic-dependent record/array
element declarations and unconstrained object actuals without a concrete
formal constraint also remain unsupported. VHDL-2019 classified interface type
syntax and default-like type declarations are rejected in VHDL-2008 mode.
This update supersedes the compact table's blanket statement that general
generic types are unavailable.

SystemVerilog time status update: compilation-unit and leading module-local
`timeunit`/`timeprecision` declarations, including the combined
`timeunit value / value` form, now override inherited `` `timescale`` context.
Decimal/scientific delays retain exact bounded rational HIR, and explicit
`fs`/`ps`/`ns`/`us`/`ms`/`s` suffixes override the module unit. Delays round
to timeprecision before exact global-tick conversion, with half steps rounded
upward; `auto` considers declarations and explicit units. Delay triplets and
explicit-unit values in each branch execute under deterministic `min`, `typ`,
or `max` project/CLI selection, with `typ` as the default. Selection precedes
precision rounding and automatic global-resolution choice. Continuous
assignments retain one, two, or three selected values for rise, fall, and
turnoff; supported gates retain one or two. One value applies to every
transition and omitted turnoff uses the smaller selected rise/fall delay.
Whole and constant packed-slice continuous writes use inertial cancellation;
packed mixed transitions take the shortest applicable delay. Procedural
delayed NBA remains transport. Parameterized or otherwise nonconstant delay
expressions remain incomplete. This update supersedes the compact table's
older fractional-delay, delay-triplet, transition-delay, and
declaration-based time limitations.

Procedural assignment status update: blocking and nonblocking
intra-assignment controls accept exact constant `#delay`, selected
`min:typ:max`, unparenthesized any-change events, scalar
`posedge`/`negedge` events, comma/`or` event lists, and wildcard RHS
dependencies. Blocking delay controls capture the RHS before suspending and
write after the delay; delayed NBAs capture immediately, do not suspend, and
publish with transport semantics in the destination update phase. Event
controls suspend before evaluating the RHS. Whole, constant packed-slice, and
blocking local targets use the same rules. Same-slot, stable cross-process,
overlapping whole/slice, `#0`, and equal-deadline NBA ordering is
deterministic, with the last staged assignment winning. Repeated
`repeat (N) @event` controls are executable and evaluate their count once
before waiting. General event expressions and parameterized/nonconstant
controls remain deferred.

Named-event status update: Verilog-2005/SystemVerilog module-level `event`
declarations, comma groups, immediate `->` triggers, static `@event`, dynamic
`@(event)`, and repeated wakeups now execute. SystemVerilog `->>` publishes
through the common update phase and `->> #delay` publishes at a future
timestamp. Same-delta source ordering deterministically distinguishes a missed
blocking trigger before its waiter, a caught waiter before a blocking trigger,
and nonblocking or zero-delay nonblocking triggers after waiters arm. Event
variables retain synchronization-object identity across blocking procedural
assignment and declaration initialization, including chained aliases and
`null`; rebinding one variable does not rebind prior copies. Triggering a null
event is a no-op. `event.triggered` remains true for the complete simulation
time step, and `wait_order` supports repeated event variables plus its ordered
success/early-failure actions. Event arguments and general event expressions
remain deferred. This update
supersedes the older broad “named events” limitation in the compact table.

SystemVerilog fork/process status update: named or anonymous bounded
`fork` blocks may contain leading packed declarations and ordered procedural
branches terminated by `join`, `join_any`, or `join_none`; matching closing
labels, `wait fork`, and `disable fork` are retained. Children start in stable
source order with independent PCs and one shared lexical frame. `join` waits
for every child, `join_any` resumes on first completion while the others
continue, `join_none` continues immediately, `wait fork` waits for live
immediate children, and `disable fork` recursively cancels all live
descendants even through a completed intermediate child. Dynamic child safe
points retain a distinct runtime ID and the static design-process identity for
bounds-safe debugger names and local schemas. Interpreter and LLVM O0/O2 share
the same frame/lifecycle behavior, callback order, VCD, and cache identity.
One live activation per lexical fork site is admitted; fork inside callables,
re-entry of a site with live children, and broader automatic per-activation
fork storage remain explicitly diagnosed bounded exclusions. This update
supersedes the compact table's blanket `fork` limitation.

SystemVerilog NBA/postponed status update: dynamic fork children stage
same-slot NBAs in stable source order, with the last staged assignment winning.
Active blocking writes, inactive `#0` work, update/NBA publication, and
postponed observation retain their exact region order. `$strobe` samples
supported direct packed-signal operands in the postponed region after NBA
publication rather than retaining an active-region formatted snapshot.
Compound or otherwise computed `$strobe` operands remain deferred with a
checked diagnostic.

SystemVerilog file-strobe status update: `$fstrobe`, `$fstrobeb`, `$fstrobeh`,
and `$fstrobeo` accept a validated process-owned descriptor plus bounded direct
packed-signal operands. The descriptor is captured when the task executes;
signal values are sampled after same-slot NBA publication in the postponed
phase and rendered with the selected default radix. Runtime-state replay,
stop/resume, interpreter, and LLVM O0/O2 cold/warm execution share the same
manifest-confined file service. Compound file-strobe operands remain a checked
diagnostic. `$fmonitor`, `$fmonitorb`, `$fmonitorh`, and `$fmonitoro` use the
same global registration and control semantics with a process-owned file
descriptor.

SystemVerilog function status update: module, package, and selected generated
functions with automatic, static, or implicit lifetime, bounded integral, byte-string, supported
container, or one-dimensional locally constant static-array value types,
ANSI or classic arguments, including named/default input values and bounded
packed writable/reference formals, now execute.
Functions may also declare one integral fixed or dynamic unpacked-array,
queue/bounded-queue, or integral-key associative-array result. Whole
function-name assignment and explicit value `return` accept exactly compatible
nonstatic whole values; fixed results additionally accept direct locally
constant colon/indexed slices and adapt equal-count ranges ordinally. Every
activation restores the exact fixed X or empty nonstatic default and isolates
the returned copy across nested nonrecursive calls. Compatible results assign
to whole module objects or automatic container locals and may flow directly
into bounded function or task input actuals.
Function bodies support nonsuspending blocks, blocking local assignments,
conditionals, exact case, canonical bounded loops, break/continue,
expressions, package imports, and directly selected package calls. Eligible
functions also fold in parameters/localparams, packed ranges, result bounds,
and generate conditions. Runtime calls use checked persistent SimIR
call/return state and produce identical interpreter/LLVM O0/O2 values,
debugger metadata, VCD witnesses, and cache behavior. Compatible direct
static-array slices copy ordinally into fixed input formals without exposing
the caller's whole array. Static and implicit lifetimes preserve bounded
packed body-scope locals across sequential calls. ANSI and classic body
declarations, named/default input actuals, packed output/inout formals,
bounded direct-local automatic `ref`, and selected generated functions now
execute. Nested or nonintegral static locals, nonlocal reference actuals,
nonintegral writable formals, multidimensional results, widths above 64 bits,
recursion, timing/event/task statements, non-byte-string types, nonstatic
slicing, general container-valued return expressions, DPI, and broader
generated visibility remain deferred.

SystemVerilog task status update: module, package, and selected generated tasks
with automatic or bounded nonsuspending static/implicit lifetime, integral,
byte-string, supported container, or
one-dimensional locally constant static-array input/output/inout formals,
ANSI or classic arguments with named/default input actuals now execute. Calls resolve
lexically, through wildcard/selected imports, or as direct `package::task`
names. Each invocation deterministically copies input/inout values into its
activation frame and copies output/inout values back only after normal or
valueless early return, including after suspension. Blocks, blocking
assignments, conditionals, exact case, canonical bounded loops, expressions,
function calls, and nested nonrecursive task calls share the common SimIR
path. Task bodies may suspend on statement-level delays, named-event
controls, and condition waits, and may schedule named-event triggers. Their
return continuations, formals, locals, and deferred copy-out survive each
wait. Compatible direct static-array colon or locally constant indexed-slice actuals use formal-typed ordinal
copy-in and atomic selected output/inout copy-out; output formals reset to
their exact default on every call. Calls are supported from `initial`,
event-controlled `always`, and
other suspending tasks; transitive suspending calls from `final`,
`always_comb`, and `always_latch` are diagnosed. Interpreter and LLVM O0/O2
agree on final state, timing/update observations, safe points, live debugger
locals across stop/resume, cold/warm reuse, and edited-task invalidation.
Static and implicit nonsuspending tasks now preserve bounded packed body-scope
locals; ANSI/classic declarations, named/default input actuals, bounded
direct-local automatic nonsuspending `ref`, and selected generated tasks
execute. Nested or nonintegral static locals, suspending static/ref tasks,
nonlocal reference actuals, widths above 64 bits, recursion, non-byte-string
types, fork/join and general event expressions, runtime-variable or
dynamic-container module-port actuals, DPI, broader generated visibility, and
cross-language calls remain deferred. Nonblocking
and intra-assignment
assignments, `$stop`, and `$finish` remain rejected inside bounded tasks.

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
postponed phase. Bounded direct packed-signal values, formatting substitutions,
monitor-list replacement, `$monitoron`, and `$monitoroff` execute through one
global runtime registration. Compound monitor operands remain diagnosed.

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
`$monitor` remain targeted. `$strobe` accepts ordered bounded conversions over
direct packed signals and samples their final committed values in the
postponed phase.
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
since that event, or `TIME'HIGH` if the signal has never changed.
`'last_active` applies the same elapsed-time contract to all transactions,
including redundant assignments. The zero-duration form of `'stable` is false
in the signal's event delta and true otherwise; a static nonnegative duration
creates one hierarchy-local implicit Boolean signal that becomes false on an
event and true after the exact quiet window. `'quiet(T)` uses the same model
for every transaction. `'active` is true for any committed signal transaction
in the current delta, including a same-value transaction for which `'event`
remains false. `'transaction` is an interned implicit Boolean signal that
toggles on every transaction, and `'delayed(T)` is an interned typed signal
whose effective-value history is transported by the exact static duration.
Implicit attributes are legal in ordinary expressions, process sensitivity
lists, and wait sensitivities. Within a process, `'driving` reports whether
that process owns a driver for the prefix and `'driving_value` reads its exact
two-, four-, or nine-state contribution; using `'driving_value` without such a
driver is diagnosed during elaboration.

VHDL identifiers are canonicalized case-insensitively. Verilog and
SystemVerilog identifiers remain case-sensitive. VHDL nine-state scalar and
vector literals retain `U`, `X`, `0`, `1`, `Z`, `W`, `L`, `H`, and `-`
through common signals, locals, structural operations, projected transactions,
signal last-value state, standard logical operators, equality, and
`std_logic` resolution in both the reference engine and generated LLVM O0/O2
code for supported values up to 64 elements. Generated code uses appended
pointer-based four-plane callbacks and explicit Logic9/Logic4 conversions;
wide-value runtime kernels remain incomplete.

For the bounded hierarchy slice, child ports alias parent signal IDs after
unique target resolution and then width, signedness, and lossy-2-state checks.
Unqualified names search VHDL, Verilog/SystemVerilog, and exported SystemC
factories across the parent logical library plus ordered
`[elaboration].search_libraries`, without a same-language or parent-library
preference. Repeated `--search-library` options replace the manifest list,
complete-scope collisions are ambiguous, unavailable configured libraries fail
only when queried, and explicit targets override inference. Automated runtime
evidence
covers both hierarchy directions: an SV top driving a VHDL counter and an SV
child, plus a VHDL top driving an SV combinational child. Bounded scalar
generic/parameter actuals cross inferred or explicit VHDL/SV boundaries before
boundary-width checks; positional actuals map by ordinal, and VHDL-associated
names use case-insensitive target matching. Selected conditional-generate
labels are retained in binding paths, so a generated child may cross into
VHDL, SystemVerilog, or SystemC under the same resolution rules. Loop-generated
children use deterministic, language-neutral `label[index]` path components;
case-generated children use their declared alternative label. Complete VHDL
generic and SystemVerilog parameter typing and sizing, general vector-direction
conversion, aggregates/interfaces, and wider wired-net behavior remain
incomplete. Exact nine-state `std_logic` and four-state `sv_wire` resolution
use process-owned driver slots across mixed boundaries. At a VHDL/SV boundary
the owning signal domain is retained and the reader/writer view applies the
documented ordinal per-element conversion in either hierarchy direction.
SystemC factories elaborate as peer hierarchy nodes under their exported
public names; `SC_FSIM_HDL_MODULE(Type)` uses the stringized type as its
inferred HDL name.

Schema 2 supports one or more independently resolved top roots. Repeated
`[[project.top]]` records retain declaration order and require unique aliases
when the list has more than one member; repeated `--top ALIAS=TARGET` options
replace that list. All roots share one scheduler, time resolution, parsed
library/package/configuration state, SystemC kernel lifecycle, trace namespace,
debugger session, and callback stream. Hierarchy paths begin with the alias,
and single-root manifests retain their former names and API view. SystemVerilog
top-level hierarchical references to packed signals on another selected root
are executable independent of root order, supporting the conventional
separate `glbl` module. Descendant-state shortcuts and cross-root references
that are not defined by the source language are rejected; expose that state
through a root-level port/signal or a language-defined package/global service.
The ordered aliased root set participates in design and native-cache identity.

Schema 2 also supports relocatable out-of-tree precompiled libraries. Each
ordered `[[library_map]]` names a logical library and `.fsimlib` directory;
repeated `--map-library NAME=DIRECTORY` options replace the manifest list.
Mapping-only projects are valid. Libraries remain unopened until elaboration
actually queries them, and declared logical dependencies require explicit
mappings. Portable VHDL and SystemVerilog units restore without reparsing and
participate in the same case, ambiguity, architecture, configuration,
parameter/generic, package, mixed-boundary, and multiple-root rules as local
units. Relocated logical source identities retain debugger and diagnostic
locations. Optional exact-host SystemC plug-ins and LLVM objects are used only
after complete compatibility and checksum validation; incompatible variants
fall back to bundled SystemC source or portable HDL. Format-1 SystemC export
accepts self-contained sources that use fsim/SystemC and standard host headers;
it rejects producer-only include paths, definitions, compiler/linker options,
or external libraries because those inputs cannot be relocated into the
portable fallback yet. See the
[precompiled-library tutorial](../examples/precompiled_library/README.md) for
the export, relocation, mapping, build, and run workflow.

The same portable VHDL, Verilog, and SystemVerilog unit representation is
available without a project manifest. `fsim compile` turns one explicitly
scripted language/standard/library compilation into a read-only `.fsimobj`;
`fsim elaborate` consumes ordered objects and explicit aliased tops to publish
a read-only `.fsimdesign`; and `fsim simulate` restores that design directly
with interpreter, compiled, or debug/O0 execution. Multiple roots,
cross-language inference, packages, contexts, configurations,
generics/parameters, source/debug locations, callbacks, and VCD retain their
ordinary project-mode semantics. `fsim systemc compile` independently compiles
one dependency-complete C++20 translation unit to `.fsimscobj`; `fsim systemc
link` combines ordered objects into a logical-library `.fsimscplugin` with a
validated factory/schema inventory. Repeated `--systemc-plugin` inputs join HDL
objects during elaboration. Selected plug-ins are embedded in format-2 designs,
then checksum-validated, reloaded, path-remapped, rebound, and lifecycle-started
during standalone simulation. Producer sources, `.fsimobj`, `.fsimscobj`, and
original `.fsimscplugin` directories are unnecessary after design publication.
The embedded native image still requires the recorded exact-compatible host,
compiler, runtime, and SystemC ABI. See the
[non-project phase tutorial](../examples/non_project_phases/README.md).

Batch 119 frontend closure now retains nested waits rather than rejecting a
successfully parsed statement tree; general assertion/report and severity
expressions; nominal file types, file objects, open-kind and logical-name
expressions across package, architecture, process, and block regions; file
subprogram-interface classes; ordinary `file_open`, `file_close`, `readline`,
`writeline`, `read`/`write`, and `endfile` call HIR; and expression-valued
physical time on waits, rejection limits, and ordered inertial/transport
waveforms. Literal report/severity and integer-unit time forms still mirror
their compact execution metadata. Nested suspension through loops,
conditionals, and exact overload-resolved procedure chains now executes with
interpreter/LLVM O0/O2 parity and rejects sensitized-process or function call
closures. General assertion/report expressions now evaluate runtime string
concatenation and `severity_level` values only on the failing path, retain
source provenance, continue through `error`, and distinguish standalone
failure publication from assertion termination in both engines. File/TextIO
HIR now lowers bounded process, procedure, and block-local file objects to
manifest-confined opaque handles. Declaration and status/nonstatus opens,
static modes, exact status ordinals, close/lifetime, file-formal state aliasing,
lookahead `endfile`, and direct signed-integer element I/O agree across the
interpreter and LLVM O0/O2. Bounded `std.textio` adds 4,096-byte `line`
buffers, newline-stripping `readline`, clearing `writeline`, whitespace- and
cursor-aware integer/Boolean/bit reads with optional `good`, and appended
integer/Boolean/bit/string writes with static side and field formatting.
The predefined nonnegative 64-bit `time` type now folds exact `fs`, `ps`, `ns`,
`us`, `ms`, `sec`, `min`, and `hr` literals, qualifications, arithmetic, and
comparisons into project ticks. Expression-valued waits and timeouts share the
same interpreter/LLVM schedule, expression units contribute to `auto`
resolution, and nonstatic, overflowing, or inexact values fail with targeted
diagnostics. Dynamic time-valued objects remain outside the bounded Batch 119
physical-time slice.
Time generics now cross same-language VHDL hierarchy as exact 64-bit
specialization values. The supported child/callable suspension path retains
source-scoped debug points and matches the interpreter, compiled O0/O2, and
forced-O0 debug engine; its parent-visible values and timestamps serialize to
the same VCD. Report and file/TextIO paths retain exact provenance across
cold/warm native-cache reuse, and projected transactions retain identical
normalized VCD and resolved-driver behavior.

Batch 120 retains the official IEEE-P1076 `1076-2019` package snapshot at
commit `16a012320947d378611cc7457f64ed76cb52bac4`. Its `ieee` and `std` VHDL
sources, Apache-2.0 license, and authorship file are byte-for-byte upstream
copies covered by checked SHA-256 values and a dependency-ordered review
inventory. An explicit `use ieee.std_logic_1164.all` now loads the pinned
declaration and body as compiler-supplied sources after verifying their exact
checksums. The supported bounded profile retains fsim's exact nine-state
`std_ulogic`/`std_logic` scalar and vector identity, complete elementwise logic
tables, standard resolution, edge predicates, and same-domain vector type
conversions. The upstream files participate in design and specialization
provenance without appearing as project-manifest sources. Project
redeclaration of a compiler-supplied package is rejected. Bundled floating
packages remain inactive until their following Batch 120 stage gains positive,
negative, elaboration, and runtime evidence; retention of an upstream
declaration name alone is not a claim that its profile is executable.

The reviewed logic-utility stage additionally covers scalar/vector
`to_bit`/`to_bitvector`, bit-to-standard-logic promotion, `to_01`, `to_x01`,
`to_x01z`, `to_ux01`, and `is_x`, including static xmap selection and all nine
input states. Static packed inputs up to 64 bits support exact binary, octal,
and hexadecimal string conversion. Dynamic string conversion and unknown
octal/hex digits remain outside the bounded profile and receive a targeted
diagnostic. Explicit `ieee.std_logic_textio` use loads its pinned alias
declaration after `std_logic_1164`; the existing bounded TextIO engine remains
the execution path for supported read/write profiles.

Explicit `ieee.numeric_std` and `ieee.numeric_bit` use clauses likewise load
their checksum-pinned declarations and bodies; `numeric_std` loads
`std_logic_1164` first. The profiles cover every positive SimIR-representable
constrained signed and unsigned vector width, with `numeric_bit` selecting a two-state element
domain and `numeric_std` retaining nine-state values. Equal-width arithmetic,
comparison, absolute value, shifts and rotates share the common packed
execution kernels. `to_signed`, `to_unsigned`, `resize`, and `to_integer`
accept arbitrary representable constrained vector widths. `to_integer` checks
the complete operand for unknown states and value fit in the predefined integer
range rather than rejecting by operand width. Direct equal-width signed/
unsigned type conversions and packed value generics preserve arbitrary-width
bits. Null arrays, unbounded result sizes, arbitrary overload profiles, and the
remaining package utilities are outside this reviewed stage.

Explicit `ieee.fixed_pkg` or `ieee.fixed_generic_pkg` use activates the pinned
`std_logic_1164`, `numeric_std`, `math_real`, `fixed_float_types`, and fixed
package sources in dependency order. The default package profile supports every
positive SimIR-representable descending constrained `ufixed`/`sfixed` width.
Locally static integer conversion aligns to an arbitrary declared binary point
and saturates without host-word masks; equal-range addition, subtraction,
comparison, and slices retain exact packed semantics. `resize` supports
arbitrary-width scale-preserving signed/unsigned resizing and nearest-rounded
unsigned fractional narrowing. Ascending fixed ranges, dynamic integer conversion, signed
fractional rounding, custom generic package profiles, wrap overflow, and the
remaining fixed arithmetic/utilities are outside this reviewed slice and
receive targeted diagnostics.

Explicit `ieee.float_pkg` or `ieee.float_generic_pkg` use extends that chain
through the checksum-pinned floating declaration/body and default package
instance. The bounded default profile is `float(8 downto -23)`/binary32.
Locally static integer conversion, default binary32 rounding, named add,
subtract, multiply, divide, square-root, comparison, integer/raw-vector
conversion, finite/NaN/unordered/sign classification, and exact signed-zero,
infinity, and NaN constructors execute through constant SimIR values in both
engines. Dynamic floating objects as operation operands, binary64/binary128 or
custom generic widths, real conversion, nondefault rounding/denormal policies,
and the remaining package utilities are outside this reviewed profile.

All reviewed packages may be collected in a reusable project context. Direct
and context-expanded `use` clauses activate the complete standard dependency
chain before manifest units, and fully qualified intrinsic calls retain their
package provenance through specialization and overload dispatch. Simultaneous
qualified `numeric_bit` and `numeric_std` types remain independently two- and
nine-state; fixed and floating default generic-package instances coexist with
their transitive logic, numeric, math, and utility dependencies. No host VHDL
package installation participates in analysis.

The VITAL dependency baseline includes transaction-aware
`'last_active`/`'quiet`/`'transaction`, time-qualified `'stable`, typed
`'delayed`, and per-driver `'driving`/`'driving_value`. Explicit or
context-expanded `ieee.vital_timing`/`ieee.vital_primitives` use now activates
clean-room compiler-supplied interfaces and exact transition, TIME-delay, map,
fixed-vector, and truth-table metadata. Zero/default constants and constrained
composite generics retain full values and cache identity.
`VitalExtendToFillDelay`, `VitalCalcDelay`, maps, BUF/INV/IDENT, tri-state
gates, arbitrary-width reductions, fixed 2/3/4 gates, MUX/MUX2/4/8,
DECODER/2/4/8, and both static `VitalTruthTable` result profiles execute in
the interpreter and LLVM O0/O2, including wide/null vectors, weak/unknown
states, pessimistic selection, artifact phases, callbacks, debugging, and VCD.
Dynamic truth/state tables are rejected. All scalar/vector setup/hold,
recovery/removal, period/pulse, in-phase skew, and out-of-phase skew profiles
execute with persistent check state, exact edge symbols, delayed sampling,
direction enables, Trigger deadlines, deterministic violation/report controls,
and cataloged static-profile diagnostics. All four `VitalStateTable` procedures
execute with first-row priority, previous-input transitions, present/next state,
unknown/no-match and retention behavior, scalar/vector results, zero state
count, null input expressions, and ascending/descending vectors. The public
path-record/array families, scalar signal delay, scalar/01/01Z wire delays, and
single/01/01Z path delays now execute with static choices and null ranges,
shortest remaining-path selection, custom output maps, default-delay controls,
fast/negative preemption, and distinct `OnEvent`, `OnDetect`, `VitalInertial`,
and `VitalTransport` pulse behavior. Per-call glitch state, X/report/severity
controls, interpreter/LLVM O0/O2/debug execution, cold/warm cache reuse,
relocatable artifacts, callbacks, and VCD use the common scheduler. VITAL
memory declarations, arbitrary-depth sparse storage, text/binary initialization,
word/subword action tables, port contention, violations, vector timing checks,
and scalar/vector memory path initialization/selection/scheduling now share the
same interpreter/LLVM state model. Cross, parallel, and subword arcs cover all
single/01/01Z/01ZX delay shapes, retain corruption, output maps, conditions,
port gating, and checked time arithmetic. Static load contents survive runtime
state, object/design artifacts, and relocation independently of the source
file. Representative configured vendor-style cell and memory models accept
VITAL_LEVEL metadata, timing generics, extended identifiers, pragmas, and null
path idioms without proprietary-name handling. SDF annotation remains in its
subsequent dedicated v2 batch.

### SystemVerilog arbitrary-width packed values in v2

Batch 151 replaces the former general 64-bit SystemVerilog constant/value
ceiling with a governed `PackedLogic4` representation. Parameters,
localparams, generated constants, enum values, packed structs and unions,
signals, variables, ports, interface/modport paths, supported multiple roots,
and signed mixed-language vector adapters retain every declared bit. Constant
arithmetic, logical and bitwise operators, comparisons, shifts, streaming,
reductions, selectors, patterns, casts, and type/object queries use checked
width and work budgets; materialized widths above 16,777,216 bits reject before
allocation.

Nested anonymous packed structs, unequal-width packed unions, tagged packed
unions, anonymous/default-base enums, recursive member initializers, and
nominal assignment/parameter/port/callable/equality/cast rules share one exact
layout and type identity. Supported functions, tasks, methods, recursion,
automatic/static locals, ref/inout/output copy-out, delayed suspension, class
properties, and process/object lifetime preserve arbitrary-width values.

Debugger show/deposit/force/release, callbacks, owning snapshots, VCD/class
traces, the public C API, binary `$fread`, and packed memory-file operations
retain exact wide values. Owning `.fsimobj`, standalone `.fsimdesign`, mapped
and relocated `.fsimlib`, class/constraint HIR, and native-cache identities
retain arbitrary-width enum values and aggregate profiles. The 137-bit binary
file fixture is a fully compiled one-process LLVM O0/O2 service-boundary test
with cold/warm cache evidence; other unsupported native value operations use
the documented per-process interpreter fallback under the same scheduler.

Limits that remain operation-specific are intentional: formatted input scan
targets and runtime-selected part widths remain 1 through 64 bits, runtime
streaming requires a locally constant slice/result within its documented
compiled representation, and nonintegral or unmaterialized container forms
remain deferred. Width/work/storage, malformed profile, lossy conversion,
stale schema, corrupt artifact, and oversized input failures are cataloged and
transactional.

This is prerequisite value/type infrastructure for UVM, not UVM completion.
Concurrent assertions and functional coverage are closed by Batches 154-155;
the bounded DPI-C boundary is closed by Batch 156 and the governed
[VPI boundary](systemverilog-vpi.md) by Batch 157. UVM library/runtime work
retains its locked later-batch ownership.
Program, clocking, and interface closure is complete in Batch 153.

### SystemVerilog concurrent assertions in v2

The v2 frontend owns `sequence`, `property`, and `checker` declarations with
formal arguments, leading local variables, declaration clocks, `disable iff`,
resolved source references, and exact token/span provenance. Typed sequence
ownership includes delays and ranges, fusion, repetition, `intersect`,
`throughout`, `within`, `first_match`, and `.matched`/`.triggered`; typed
property ownership includes implication, delays, until/nexttime families,
recurrence, strength wrappers, and synchronous/asynchronous accept/reject
aborts. Concurrent assert, assume, cover, and restrict directives retain region
policy, stable names, action blocks, controls, semantic observer descriptors,
and deterministic coverage slots.

The executable slice accepts inline packed predicates and named property or
sequence value profiles with positional, named, and default actuals. Integral
property/sequence locals are exact attempt-owned storage: initializers run once
per forked attempt, and `first_match` match items execute ordered blocking,
compound, increment/decrement, or subroutine-call actions only when the scalar
or governed fixed/ranged two-element sequence completes. One direct
design-unit clock object may carry an optional edge. The resulting ordinary
source-spanned assertion processes share the existing debugger and report path.
Public per-instance coverage and pass/failure/disabled sample events feed
callbacks and trace backends identically across interpreter, LLVM O0/O2,
multiple roots, standalone artifacts, relocation, and warm native caches.
Assertion on/off/kill, pass/failure action, vacuity, and constant
`$assertcontrol` policies execute through the same engine-neutral path.

Invalid assertion value profiles, nonintegral local types or initializers,
match-item targets outside attempt locals, predicates outside the executable
packed-expression subset, and compound clock events reject with stable
diagnostics. Directive and local packed widths have no language-count or
host-word ceiling; governed work/storage boundaries remain explicit resource
policy. Richer temporal forms outside the executable subset remain exact
source-owned semantic HIR and are diagnosed rather than silently omitted.

### SystemVerilog functional coverage in v2

Design units and classes own SystemVerilog-2017 covergroups with constructor
formals, event or `with function sample` profiles, instance/type options,
coverpoints, crosses, exact source provenance, and stable specialization and
runtime identities. The bounded executable slice supports scalar, inclusive
range, wildcard, sized/unsized array, automatic, default, ignored, illegal, and
transition bins; deterministic guards and overlap handling; automatic cross
products; and explicit cross bins using `binsof`, complement, Boolean
composition, scalar/ranged `intersect` selections, and `with` predicates with
optional `matches(n)` or `matches($)` cardinality over governed exact candidate
domains. Coverpoint-bin `with` filters are applied before array-bin
distribution and retain arbitrary-width signed value and X/Z planes.

Weights, goals, `at_least`, `per_instance`, and `merge_instances` use exact
basis-point percentages with instance-over-type precedence. Explicit,
event-driven, and procedural sampling share one transactional scheduler and
ordered pre-sample, hit/illegal/cross, and post-sample callbacks. Structured
and deterministic text reports expose type, instance, coverpoint, cross, bin,
hit, exclusion, illegal, threshold, goal, percentage, and source state.
Debugger snapshots and traces add multiple roots, canonical aliases,
VCD-compatible values, and exact time/delta coordinates.

Coverage declarations and live hit/progress/report/observation state survive
`.fsimobj`, standalone `.fsimdesign`, mapped-library relocation, multiple
roots, and cold/warm LLVM O0/O2 reuse. Static declaration, aggregate-bin,
cross-product, and transition-work limits plus transactional input and
persistent-state limits reject before partial mutation. The supported formal
and sampled value model is bounded integral data; real/string/chandle/event/
void formals and unrestricted coverage-driven randomization remain outside
this slice.

### SystemVerilog class foundation in v2

The current v2 slice owns and resolves compilation-unit, package, module,
interface, and nested class declarations. It supports forward typedefs,
value/type parameters, parameterized bases, properties with visibility and
static/const/random qualifiers, constructors, functions, tasks, extern and
pure-virtual prototypes, out-of-block definitions, executable constraints in
canonical typed HIR, single inheritance, interface implementation, hiding, checked overrides,
covariant class-handle returns, and stable virtual dispatch slots.

Executable support includes opaque nullable/generation-safe handles,
base-to-derived construction, property access, assignment/equality and checked
casts, instance/static methods, automatic frames, recursion guards, task
suspension, one static store across multiple roots and import aliases, and
bounded handle elements in fixed/dynamic arrays, queues, associative arrays,
unpacked aggregates, and class properties. Packed properties participate in
callbacks and debugger reads, and class state/provenance survives portable
objects, standalone designs, mapped libraries, relocation, and native-cache
reuse. Resource rejection is based on caller budgets and checked materialized
storage, not an arbitrary class/container element limit.

Source-executable class expressions include `new`, `null`, assignment,
equality, `$cast`, instance and static property selection, constructors,
ordinary/static/nonvirtual/explicit-base/virtual functions, and suspending
tasks. Instance and static class functions preserve packed and mutable-string
input, output and inout actuals through the same ordered copy-in/copy-out
contract; source-evaluated automatic and static string locals retain exact
bytes without a fixed string-value width. Class handles pass through module
functions/tasks, generated recursive
hierarchy, multiple roots, and typed fixed/dynamic/queue/associative
containers. Interpreter, LLVM O0/O2 service boundaries, debugger inspection,
packed callbacks/VCD snapshots, portable artifacts, relocation, and cold/warm
native caches share the same canonical identities and behavior.

The implemented randomization slice includes `rand` and `randc`, per-object
property `rand_mode` and block `constraint_mode`, object `randomize` with an
optional property list, scope `std::randomize`, pre/post callbacks, inheritance
and constraint-block override composition, exact public/protected/local
access, deterministic per-root/object/property/call streams, and transactional
failure. Source `randomize with { ... }` blocks retain portable expression
templates rather than being discarded; class and scope calls bind their names
to exact solver variables at execution and preserve arbitrary-width constants,
solve ordering, implication, distributions, `inside`, soft preferences, and
structured conditionals through design artifacts and native-cache identity.
The standard 32-bit random-distribution functions `$dist_uniform`,
`$dist_normal`, `$dist_exponential`, `$dist_poisson`, `$dist_chi_square`,
`$dist_t`, and `$dist_erlang` are also executable. Their first argument is a
writable packed integer seed of at least 32 bits; each call writes back the
standard updated seed and returns the standard signed 32-bit result.
The standard `$system` service accepts zero or one command string and can be
used as either a task or a function. It executes as a serialized host boundary;
function use receives the raw signed 32-bit return from C `system()`, task use
discards that value, and the no-argument form preserves `system(NULL)` rather
than substituting an empty command. `Simulation` installs the native C executor
by default and exposes an overridable hook so an embedding can intercept or
disable process invocation without changing HDL or compiled semantics.
The legacy stochastic-analysis family `$q_initialize`, `$q_add`, `$q_remove`,
`$q_full`, and `$q_exam` is also executable. Queue IDs name simulation-owned
FIFO or LIFO state, capacity and duplicate checks return the standard status
codes, and length/interarrival/occupancy/wait statistics use deterministic
simulation ticks. All arguments and results retain the standard signed 32-bit
integer profile across interpreter, LLVM, and runtime artifacts. Queue-object
and stored-entry allocation shares the explicit simulation-owned container
storage budget; exhaustion reports standard status 7 rather than imposing a
language-level queue length limit.
All sixteen programmable-logic-array tasks are executable: synchronous and
asynchronous AND, NAND, OR, and NOR over both array and plane personality
formats. The personality is an ascending one-dimensional fixed packed memory
whose element width matches the input terms and whose depth matches the output
terms. Packed inputs, outputs, and words are dynamically sized, including
widths above 64 bits. Asynchronous forms update without delay whenever an input
term or any personality word changes; synchronous forms update only when
called. Interpreter and LLVM use the same evaluator and four-state rules.
The IEEE four-state VCD control family is executable from Verilog-2005 and
SystemVerilog: `$dumpfile`, `$dumpvars`, `$dumpoff`, `$dumpon`, `$dumpall`,
`$dumplimit`, and `$dumpflush`. `$dumpvars` supports whole-design dumping or a
level plus module/variable selection list, starts at the end of its simulation
time unit, and requires all invocations to share that time. Suspension and
resumption emit standard unknown/current checkpoints; `$dumpall` emits an
unconditional current-value checkpoint, the default path is `dump.vcd`, and a
byte limit terminates dumping with a VCD comment. Paths remain sandboxed below
the project file root and packed values retain their complete declared width.
The companion extended-VCD family is also executable: `$dumpports`,
`$dumpportsoff`, `$dumpportson`, `$dumpportsall`, `$dumpportslimit`, and
`$dumpportsflush`. Each `$dumpports` call owns one unique file and one or more
unique module scopes, includes only ports declared directly by those scopes,
and begins at the end of the common invocation time. Extended node declarations
preserve packed indices; value records preserve complete arbitrary-width port
states, input/output direction codes, and resolved zero/one strengths. Optional
filenames address one file or all files as specified by the standard, and every
closed file records its exact final simulation time with `$vcdclose`.
The finite-domain solver supports integral, packed four-state, enum,
handle, and materialized container-element variables; equality, relational,
arithmetic, bitwise, logical, unary, and conditional expressions; `soft`,
`inside`, `dist :=`, `dist :/`, implication, structured blocks, `foreach`, and
solve-before ordering. Width/sign conversion and unknown predicate behavior
are explicit. Exact-domain `randc` cycles, modes, seeds, revisions, solver
provenance, callbacks, debugger/trace state, portable objects, standalone
designs, relocated mapped libraries, and native-cache identities share one
checked representation.

Randomization is intentionally resource-governed: callers supply
variable, aggregate-domain, search-step, elapsed-work, heap, and cycle-storage
budgets. Packed values and constraint constants have no host-word language
limit; the public SimIR width field remains the explicit physical
representation boundary, and finite exact-domain expansion may report governed
resource exhaustion independently of declared width. Nonintegral random
variables, unmaterialized/unbounded containers, arbitrary user-defined solver
functions, and coverage-driven solving remain outside this slice. Scope
randomization executes through the common validated solver service while its
full portable operation participates in artifacts and native-cache identity.
This is executable class randomization, not full UVM
closure; UVM library/runtime behavior remains for subsequent v2 batches.
Batches 156-158 close the governed DPI-C, VPI, and VHPI boundaries. Batch 155
closes the bounded covergroup surface described above.

### SystemVerilog scalar, Unicode string, and foreign-handle closure in v2

The executable scalar family now distinguishes `shortreal`, `real`,
`realtime`, `time`, and `chandle` in declarations, typedefs, parameters,
package constants, nets/variables, ports, interface and module callables,
class members, aggregates, and supported containers. Canonical decimal/time
literals retain enough payload to perform deterministic contextual conversion.
Dependent constants, imports, overrides, generated declarations, static and
automatic callable locals, copy-in/copy-out, recursive composite profiles, and
independently specialized roots preserve exact scalar kind and payload.

Binary32/binary64 arithmetic, exact time ticks, comparisons, casts,
classification, explicit rounding, locale-independent formatting/scanning,
typed scheduling, and `$time`/`$stime`/`$realtime` execute through common
checked services. Interpreter and LLVM O0/O2 agree on canonical payloads,
signal transport, callbacks, debugger show/deposit/force/release, snapshots,
and VCD real versus exact-vector declarations. Overflow, nonfinite rejection,
unsupported operators, and resource failures are transactional.

`chandle` supports null and simulation-owned opaque identities, equality and
inequality, exact same-kind transport, debugger inspection/mutation, callbacks,
and tracing. Generation checks reject stale or foreign handles, cleanup is
one-shot, and registry state never serializes a host pointer. Non-null text or
binary input and numeric/arithmetic uses remain checked errors. DPI-C, VPI,
and VHPI connect standardized external ownership in Batches 156-158.

Mutable strings index Unicode scalar values through strict UTF-8. The standard
length, iteration, indexing, slicing, replacement, case, comparison,
substring, integer-conversion, and real-conversion methods share bounded,
transactional storage. Scalar text and binary file I/O covers the supported
real/time/string/null-chandle values with descriptor and partial-conversion
rollback. Cross-language strings, unrestricted host paths, standard descriptor
aliases remain deferred. Batch 152 adds strict-
UTF-8 string associative indices and process-owned multichannel descriptor bits
for the supported bounded operations.

Owning-unit schema 8, portable-library schema 5, design-state artifacts,
relocated mapped libraries, and native-cache identities preserve these types
and values. Malformed scalar enumerations, schema/checksum damage, producer-
absolute provenance, and partial payloads are rejected before publication.
This scalar closure is prerequisite infrastructure, not UVM completion:
arbitrary-width packed values and residual aggregate/procedural work close in
Batches 151-152, SVA/coverage close in Batches 154-155, foreign interfaces are
owned by Batches 156-158, and UVM 1.2/UVM 2020-3.1 by Batches 159-162.

### SystemVerilog unpacked data, file, and procedural closure in v2

Batch 152 makes remaining-rank multidimensional prefix selections, range and
indexed slices, overlapping snapshot copies, compatible callable returns and
copy-out, recursive unpacked struct/union values, nested assignment patterns,
string-element containers, and canonical string-index associative arrays
executable. Supported recursive fixed profiles cross VHDL/SystemVerilog ports
with explicit direction and shape checks. Multichannel output and bounded
multidimensional, string, and recursively packable aggregate memory-file I/O
stage complete results before publication.

Runtime packed, `time`, `real`, `shortreal`, and `realtime` delay values are
normalized and rounded at execution. Edge-qualified packed expressions may be
mixed with direct signals in event lists, and runtime-selected force/release
bit targets preserve underlying drivers. Nonlocal and selected ref actuals are
captured once across suspension; nested packed/string/container static locals
initialize once, and static tasks may suspend sequentially. Re-entered fork
sites own simultaneous children and generation-safe `process` handles with
checked self/status/completed/await/kill/suspend/resume,
`get_randstate`/`set_randstate`, and `srandom` operations.

Contextual typed mailboxes and counting semaphores provide bounded FIFO
blocking/nonblocking operations and fair wakeup. Named-event waiters retain
stable process order, and no-argument container `shuffle()` consumes the
deterministic process random stream identically in interpreter and compiled
execution. Cancelable scheduler work is owner-checked, invalidates on every
terminal path, and remains resumable after a propagating callback failure.
Runtime-state schema 16, native-object schema 87, portable objects/designs,
relocated mapped libraries, debugger/callback/VCD surfaces, and cold/warm LLVM
O0/O2 cache execution preserve this completed substrate.

This is governed procedural infrastructure, not complete SystemVerilog/UVM.
Batch 153 supersedes this slice's program, clocking-block, and interface
exclusions, and Batches 154-155 close their bounded SVA/coverage surfaces.
Foreign interfaces and UVM retain their locked later-batch ownership;
unrestricted allocation, standard descriptor aliases, and ordering `with`
clauses on `shuffle()` remain outside this slice.

### SystemVerilog program, clocking, and interface closure in v2

Batch 153 makes `program` a distinct design-unit kind throughout parsing,
semantic HIR, root selection, specialization, hierarchy, artifacts, and debug
identity. Program processes execute in the reactive scheduler region after
active/inactive/update and observed work and before re-inactive, re-update,
and postponed observation. Initial, timed, event, sensitivity, delta, and fork
resumes retain that ownership. A program `#0` resume uses re-inactive in the
same time slot and program nonblocking updates use re-update; module and
program final blocks execute exactly once in deterministic active/reactive
order.

Clocking blocks retain their event, default and per-signal input/output skews,
edge qualifiers, `#1step`, time delays, aliases, and declared directions on
modules, interfaces, and programs. Input members sample into owned storage in
the observed region after ordinary updates; output members project checked
delayed requests to their driven signals. A declared default clock lowers
procedural `##` controls to repeated clock-event occurrences in the owning
process region. Modports may expose the clocking block while
preserving its event and sampled/driven member views through recursive module
boundaries.

Virtual-interface variables and class properties retain the concrete
interface type, parameter actuals, and optional modport restriction. Static
interface-array elements and recursively forwarded generic or restricted
ports preserve a deterministic nonzero pointer-free identity. Repeated views
of one specialization compare equal, distinct elements or specializations
compare unequal, and a restricted actual cannot widen through a generic port
or rebind to another modport.

Interpreter and cold/warm LLVM O0/O2 execution agree on program scheduling,
clock samples, direct and forwarded virtual handles, callbacks, debugger
identity, and VCD. Owning `.fsimobj`, standalone `.fsimdesign`, relocated
artifacts, and native-cache reuse preserve the same state. Owning-unit schema
10, portable-library schema 7, runtime-state schema 17, semantic-state schema
2, DesignIR-state schema 2, and class-state schema 7 reject future or corrupt
payloads before publication.

### Older Verilog and SystemVerilog service profiles in v2

Explicit Verilog-1995, Verilog-2001, Verilog-2001-noconfig and
SystemVerilog-2005/2009/2012 selections retain their own predefined system
task/function environment. Verilog-1995 keeps legacy display, monitoring,
single-argument `$fopen`, memory-read, random and time services. Verilog-2001
adds the bounded descriptor-input/positioning, plusarg, memory-write,
format-to-variable and signed-conversion families, including the
two-argument `$fopen` signature; Verilog-2005 adds `$clog2` and the supported
mathematical family. SystemVerilog-2005 adds severity, sampled-value,
coverage, packed/array introspection, string formatting, host-command and
unsigned-random services. Global-clock sampled functions and extended
assertion controls begin in SystemVerilog-2009.

The parser records exact 32/64-bit integer, one-bit predicate, real, time and
string result profiles before elaboration. Display/write operations remain in
the active region while strobe/monitor operations retain postponed scheduling;
severity and assertion-control identities lower unchanged. A later-only
service or signature produces `FSIM-SV-PARSE-350` at the owning system name.
Compatibility switches do not widen this service set or authorize a later
signature.

### Older DPI and VPI profiles in v2

DPI import/export declarations begin in SystemVerilog-2005. Each declaration
retains the selected standard revision; a Verilog selection rejects the DPI
surface with `FSIM-SV-PARSE-351` even when `import` or `export` is otherwise an
ordinary identifier in that keyword profile.

Published VPI objects retain the exact Verilog-1995, Verilog-2001,
Verilog-2001-noconfig, Verilog-2005, SystemVerilog-2005, SystemVerilog-2009,
SystemVerilog-2012 or SystemVerilog-2017 identity of their owning unit.
SystemVerilog-only packages, interfaces, programs, classes, class properties,
assertions and descriptors cannot be registered under a Verilog identity.
The original numeric identities for Verilog-2005 and SystemVerilog-2017 remain
zero and one, respectively, so the expanded profile enum is append-only for
existing ABI consumers. Exact packed widths, four-state values, hierarchy
ownership, traversal order and callback behavior are unchanged. Compatibility
switches do not manufacture a later DPI or VPI surface.

### Older-mode compatibility defaults in v2

The seven explicit Verilog/SystemVerilog compatibility switches are an
independent, canonical dimension of the selected standard. Input aliases and
order reduce to the fixed `keyword-profile`, `implicit-net`, `port-connection`,
`sizing`, `lifetime`, `scheduler-assertion`, `configuration` order; `none`
remains distinct. The exact profile enters preprocessing, parsing,
analyzed-unit provenance and every project, semantic-dependency, specialization
and native-cache identity.

`keyword-profile` selects only the documented legacy keyword environment:
Verilog-2005 uses the Verilog-2001 set, and SystemVerilog-2009/2012/2017 use the
SystemVerilog-2005 set. `configuration` restores the optional Verilog-2001
configuration keywords only for the explicit Verilog-2001-noconfig revision.
The remaining switches name the established implicit-wire, omitted-port,
parameter/expression-sizing, static-lifetime and active/postponed assertion
scheduler compatibility defaults. They are provenance-bearing semantic choices
and do not change the selected grammar or system-service revision.

Compatibility switches compose by the fixed canonical order. A preprocessed
token stream retains the same selected revision and exact profile at parser
entry; a 257-bit declaration remains exact. Unknown switches and cross-family
standards retain the source-owned `FSIM-PROJ-0007` diagnostic. Even the complete
profile cannot admit a later `nettype` declaration, DPI surface, system service
or VPI object. The selected revision remains the upper language boundary.

Analyzed libraries and portable artifacts retain that boundary per source and
owning unit rather than inferring it from a language-wide default. Library
format 3 and portable schema 10 index the exact standard and compatibility
profile beside each source checksum and unit payload; owning-unit schema 26
retains the same identity inside modules, UDPs, classes, nested classes and
methods. Object format 5 binds the ordered source/include digests and unit
profiles into its compilation digest. Design format 8 adds an ordered,
duplicate-free Verilog/SystemVerilog semantic-unit profile table matched to its
object provenance. Loading rejects omitted, stale, reordered, duplicated or
profile-incompatible records before publishing the restored semantic design.
Mapped libraries reconstruct distinct source settings for every retained
standard/profile pair, including source-hidden portable units, so consumer
cache identity cannot collapse two analyzed profiles.

The same exact identities survive execution boundaries. Each of the six older
Verilog/SystemVerilog modes runs the complete canonical seven-switch profile
through interpreter and LLVM O0/O2 while crossing VHDL-2008 and native SystemC
instances. A separate SystemVerilog-2017 `none`-profile root executes in the
same design without inheriting the older root's revision or switches. Exact
137-bit four-state values, including X and Z planes, survive both language
crossings and a Verilog specify path; VHDL PSL observation, one-nanosecond
scheduling, debugger values and VCD changes remain identical between engines.
Revision-distinct specialization keys prevent native-cache sharing across the
six profiles. Existing mixed-conversion and hierarchy/configuration evidence
continues to cover signed and unsigned extension, two-/four-state conversion,
Boolean/integer adapters, configuration and bind selection.

Public Batch 167 provenance joins every elaborated Verilog/SystemVerilog scope
to its owning semantic unit and source, canonical revision and compatibility
profile. The C++ simulation API, append-only C object and safe-point records,
Tcl `fsim::provenance`, debugger `provenance`, VPI type metadata and VCD
comments expose the same identity. Descendant objects inherit their owning
scope identity; cache keys, compiler objects and other implementation details
remain absent from hierarchy discovery. Partial VPI provenance is rejected.

That public identity remains stable across the durable Batch 167 boundaries.
Direct object builds, standalone design reload, interpreter and LLVM execution,
cold/warm native caches, checkpoint serialization/replay and relocation report
the same semantic unit/source IDs, canonical revision and compatibility
profile after the original source and producer object paths are unavailable.
The six-mode artifact matrix covers Verilog-1995, Verilog-2001,
Verilog-2001-noconfig and SystemVerilog-2005/2009/2012 with an explicit
`sizing` profile; a separate exact-width path retains 137-bit X/Z and signed
values through the same boundaries and filtered VCD comments. Omitted, partial
or object-inconsistent unit provenance rejects before a restored design is
published.

The authoritative Batch 167 corpora are published beside the inventories.
`verilog_systemverilog_revision_corpus.tsv` assigns every older revision one
positive parse witness, one negative witness with an exact diagnostic code and
coordinate, and anchored execution, arbitrary-width, include provenance and
artifact-mismatch evidence. The companion
`verilog_systemverilog_compatibility_corpus.tsv` assigns the same evidence
classes to each of the seven explicit switches. Its frontend witness exercises
every switch independently: each retains its exact canonical profile and a
257-bit declaration while rejecting later `nettype` grammar at
`FSIM-SV-PARSE-346` 1:31. The registered inventory gate validates row order,
diagnostic triples and every file/anchor pair so documentation-only claims
cannot replace executable evidence.

The serial `fsim.verilog-systemverilog-standard-mode-closure-matrix` composes
those corpora with frontend and application conformance, six-mode mixed
interpreter/LLVM O0/O2 execution, cold/warm caches, standalone artifacts,
relocation/checkpoint replay, public C/C++/Tcl/VPI surfaces, runtime, and the
MSVC/Windows/tool/resource contracts. It retains a stage ledger plus one
verbose log for each of 16 witnesses. Exact transcript tokens bind all six
revisions, the complete explicit profile, 137-bit X/Z values, mixed VHDL and
SystemC, hidden artifact producers, cache reuse and the governed 6-GiB,
delta-1000 and VCD-64 ceilings. A passing child exit without those transcript
tokens cannot satisfy the matrix.

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

Deferred beyond v1: PSL, VHDL-AMS, SDF, proprietary package or pragma
semantics, and VHPI extensions outside the governed
[Batch 158 contract](vhdl-vhpi.md).

### Verilog-2005 and SystemVerilog-2017

Required for v1:

- the preprocessor, modules, interfaces/modports, packages, parameters, and
  generates;
- nets, variables, packed and unpacked types, structs/unions/enums, and
  memories;
- gate primitives, continuous/procedural assignments, and all `always` forms;
- combinational and sequential UDP declarations, ordered truth/state tables,
  static instance arrays, and one/two/three-value propagation delays;
- functions/tasks, `initial`/`final`, delays/events, fork/join, and named
  events;
- strings, files, dynamic/associative arrays and queues;
- deterministic `$random`, `$urandom`, and `$urandom_range` streams seeded
  per stable process ID, `$readmem*`, display/stop tasks; and
- immediate assertions.

Deferred beyond v1: classes, constraints and UVM; concurrent SVA; covergroups;
DPI profiles outside the bounded [Batch 156 contract](systemverilog-dpi.md),
VPI extensions outside the governed [Batch 157 contract](systemverilog-vpi.md),
and SDF annotation. Program and clocking blocks are implemented by the v2
Batch 153 profile described above.

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
explicit hierarchy metadata and one common signal identity.
`SC_FSIM_HDL_MODULE` children bind like normal modules but are lowered at their
existing path to the explicitly manifest-selected VHDL or SV implementation.
`SC_FSIM_EXPORT` and `SC_FSIM_EXPORT_AS` publish one or more factories through
a deterministic descriptor registry and automatically discover an optional
factory-parameter schema. The legacy `hdl_instance`, handwritten entry point,
and `register_module_factory` surfaces remain compatible and deprecated.
General custom interface metadata and broader channel behavior remain v1
targets. TLM, AMS,
CCI, dynamic processes, arbitrary custom primitive-channel
interfaces/binding beyond the bounded registered `sc_prim_channel` update
callback, and Accellera kernel/ABI compatibility are deferred.

## Release evidence

The promise above becomes v1 only when a checked-in feature matrix maps every
required construct to positive, negative, elaboration, and runtime tests.
Every semantic simulation test must run through both the SimIR interpreter and
LLVM JIT with identical final values, assertions, scheduling observations, and
trace changes on Ubuntu x86-64/GCC and Windows x86-64/MSVC.
