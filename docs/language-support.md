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
| VHDL units | `library`/`use`/context-reference clauses retained on their following unit; reusable context declarations containing bounded context items; package declarations containing bounded constants, subtypes, non-nested records, user-defined enumerations, one-dimensional scalar-element arrays, and scalar function/procedure declarations, plus matching bounded package subprogram bodies; bounded generic package declarations and entity/architecture-local package instantiations over existing scalar value/type/function/procedure generic families; bounded generic function/procedure templates and local or package-visible instantiations over those families; entities and architectures with package/entity/architecture type and subtype declarations over scalar logic/bit/Boolean, enumerations including ascending/descending constraints, constrained signed/unsigned or logic/bit vectors, constrained or `integer`/`natural`/`positive range <>` user arrays, portable integer ranges, and bounded records; scalar integer/Boolean/bit/enumeration or subtype-typed generics; VHDL-2008 unclassified interface type plus bounded interface function, procedure, and package generics; parameterized packed and enumeration ranges; record/subtype/enumeration/user-array or packed scalar/vector ports, signals, and bounded packed process variables; and `integer`/`natural`/`positive`/explicit integer-range ports, signals, and process variables; direct-entity and component-style instances with positional-then-named `generic map` actuals, labeled `if`/`else`, integer-range `for`, scalar/inclusive-range-choice `case` generate regions, and unguarded labeled block statements containing bounded constants, local packed signals, concurrent assignments, processes, instances, and nested regions | Recursive acyclic `context library.name` expansion; explicit use visibility and direct `package.item`/`library.package.item` references for declaration-ordered scalar constants and bounded record/subtype/enumeration/array declarations and literals, including precise transitive context/package source provenance; directly visible package functions and procedures merged from matching bounded bodies; independently resolved entity and architecture type regions with entity subtype visibility in the associated architecture; chained subtype resolution, derived integer/enumeration-base containment, packed reconstraint legality, specialization-dependent packed/enumeration/array bounds, subtype-left defaults, checked enumeration constants/generics/stores, nominal user-array values, and directionally safe hierarchy aliases; declaration-order record layouts and minimum-width nominal enumeration ordinals, element-domain defaults, same-language nominal port aliases/copy/comparison, enum-typed constants/generics, contextual identifier/character literals and case choices, record/array subtype aliases, contextually typed positional/named/final-`others` record aggregates, member/index/slice access, persistent debug-visible record/subtype/enumeration/array locals, and explicit mixed-boundary rejection; recursively elaborated per-occurrence value/type/function/procedure/package generic specializations with named/positional whole-signal `port map` associations, declaration-ordered local package and generic-subprogram specialization, selected package constants/types/functions/procedures, exact interface-package and instantiated-subprogram forwarding with transitive body provenance, declaration-ordered generated-constant and pure-function folding, interval-based selection with null-range handling, specialization-selected or always-selected scoped behavior with scope-qualified locals, persistent process-variable registers, and signed 32-bit two-state integer-family objects with specialized constraints, subtype-left initialization, and range-safe aliases | Complete package bodies and general package subprograms, general or nested generic package units, nested generic subprogram templates, standard-library context/package loading, and general visibility/overload resolution are not implemented; record types remain non-nested and packed and exclude integer/access/protected elements, nested aggregates, element-choice groups, and qualified aggregate expressions; user arrays remain one-dimensional with scalar bit/Boolean/std_logic/std_ulogic elements and exclude composite/enumeration elements, array aggregates, dynamic slices or chained selections, and null executable objects; enumeration literal visibility is currently contextual rather than a complete overload candidate-set implementation; package/context visibility cycles are rejected; no configurations, generated/scoped or mixed-language interface-package/subprogram actuals, VHDL-2019 classified interface types, process/generate-local subtype declarations, physical types, guarded blocks, nonintegral case-generate choices, generated subprograms/types or other declarative items, expression/`open` port actuals, or process declarative items beyond bounded variables; scalar constraints outside the portable signed 32-bit interval are rejected |
| VHDL statements | Concurrent assignments, labeled/unlabeled `with`/`select` selected signal assignments and concurrent assertions, process sensitivity lists, `if`/`elsif`/`else`, ordered packed `case`/`when`/`others`, locally static sequential `for`, Boolean `while`, unconditional and labeled loops, conditional/targeted `exit` and `next`, simple and chained VHDL-2008 conditional signal/variable assignments, signal/variable assignment including one-level record-element targets, `null`, ordered waveform elements, `unaffected` alternatives, exact integer `after`, implicit/explicit `inertial`, `transport`, and `reject TIME inertial`, top-level bare/`on`/`until`/`for` wait clauses in every legal combination, and sequential `assert` with an optional literal `report` and standard severity | Whole-record and whole/constant-selected packed signal/local assignments, contextually typed record-aggregate initializers and whole-object values, constant record-member/index/slice writes, dynamic single-element signal/local writes with signed 32-bit `integer` indices and execution-time index capture for delayed/projected waveforms, source-ordered Boolean conditional-assignment alternatives including record aggregates, selected concurrent assignments with grouped exact choices, final `others`, independent ordered waveform lists or `unaffected` on every conditional/selected alternative, and inferred reactive sensitivity; complete new signal waveforms atomically edit cancelable projected transactions per scalar subelement, with transport truncation and exact inertial mark/delete using an explicit rejection limit or the first delay by default; nested Boolean-typed conditional branches, exact `|`-separated case choices with nested statement lists, ascending/descending/null `for` ranges with implicit-constant substitution and bounded unrolling, executable loop backedges with persistent locals, nested/named loop transfers, edge-guarded clock processes, first-suspending condition waits, event-or-timeout races with preserved absolute deadlines, Boolean assertions with nonfailure severities continuing, reactive concurrent-assertion sensitivity, integer/logic/vector/Boolean literals, and selected operations | Bounded selected assignments require a final `others` and do not support matching-select `?`; `null` waveform elements require guarded-signal driver disconnection and are diagnosed as unsupported; no conditionally nested waits, case ranges, array aggregates, dynamic slices or nested/chained dynamic selections, nested/chained record selections or local scopes, general report expressions/statements, configurable assertion stop levels, or complete LRM physical-time semantics; bounded integer time units are normalized exactly and waveform times must be strictly ascending |
| VHDL expressions | Identifiers and one-level selected record names, decimal/logic/string/Boolean literals, identifier/character enumeration literals, record aggregates, calls, index/slice syntax, common unary/binary syntax | Identifiers/literals and objects resolved through bounded scalar/vector/record/enumeration/user-array subtype chains; contextual enumeration literals in initializers, assignments, conditional alternatives, comparisons, and case choices with nominal identity and ordinal relational ordering; enumeration type/subtype-mark `left`/`right`/`low`/`high`/`length`/`ascending` and checked `pos`/`val`/`succ`/`pred`/`leftof`/`rightof` over each resolved ascending or descending subtype range, including package/generic folding, base-declaration ordinals, and dynamic failures; contextually typed positional/named/final-`others` bounded record aggregates in initializers, whole-object assignments, equality/inequality, and conditional alternatives; whole nominal user-array copy/equality plus constant array indexing/slicing and dynamic single-element indexing with signed 32-bit `integer` indices and declared-range mapping; record-member reads with constant element bit/slice selection, unary plus/minus, checked integer-family and bounded packed signed `abs`, Boolean and packed `not`/`and`/`or`/`xor`, Boolean `nand`/`nor`/`xnor`, equal-width signed/unsigned packed arithmetic plus checked signed 32-bit integer-family `+`, `-`, `*`, `/`, `rem`, `mod`, and locally static nonnegative `**`, equality/inequality/relational comparisons including whole same-record-type equality, packed `sll`/`srl`/`sla`/`sra` and `rol`/`ror` with locally static or dynamic integer counts and negative-count reversal, constant in-range indexed names/slices with declared-range mapping, and width-summing packed `&` concatenation | Selected/qualified type marks in attribute prefixes, qualified expressions, dynamic/negative integer exponentiation, dynamic slices, most calls/operators, array/nested/choice-list aggregates, and complete overload/self-determined sizing are not lowered; out-of-range or unknown dynamic indices fail at runtime; explicitly mixed signed/unsigned numeric operands require conversion |
| Verilog/SV units | Modules; bounded packages containing integral parameters/localparams, packed integral typedef aliases, packed enums, non-nested packed structs, equal-width packed unions, imports, automatic integral functions, and automatic integral tasks; compilation-unit or unit-local wildcard/selected imports; ANSI and basic non-ANSI packed ports plus one-dimensional integral static-array, dynamic-array, queue, bounded-queue, and integral-key associative-array ports, nets/variables, packed constant or parameterized ranges, integral value parameters/localparams using implicit, `byte`, `shortint`, `longint`, `time`, `int`/`integer`, or packed `bit`/`logic`/`reg` types with explicit signedness, bounded same-language `parameter type`/`localparam type` declarations, module instances with named or positional value/type overrides, explicit or implicit conditional/inline-or-module-`genvar` iterative/constant-choice generates, and direct or named static contents in explicit generate regions containing bounded parameters/localparams, local packed signals, continuous assignments, processes, instances, and nested regions | Recursive case-sensitive package imports, `package::constant` folding, imported/scoped/local typedef resolution, enum enumerator visibility, declaration-order packed-struct layout, and offset-zero packed-union overlay layout for parameters, ports, signals, generated signals, and procedural locals with precise source provenance; explicit/implicit enum values are checked for packed base fit/uniqueness; typed 1–64-bit parameter defaults, overrides, localparams, and non-iterative generated parameters preserve width, signedness, state domain, and the complete unsigned-64 range after specialization-dependent range folding; bounded type parameters resolve integral builtins, local/imported/package-selected typedef marks, dependent packed ports/signals/typedefs/value constants, and nested formal forwarding with `sv-type-v1` cache identity; bounded module/package automatic functions and tasks admit integral, byte-string, dynamic/queue/associative-container, and one-dimensional locally constant static unpacked-array values, with module objects/automatic locals, whole or direct compatible slice value-copy actuals, nested nonrecursive calls, suspension-safe atomic copy-out, and transitive source provenance; direct same-language whole-container port actuals preserve exact specialized element, kind, queue-bound, associative-index, or static-range metadata, while compatible direct static-array colon or locally constant indexed-slice actuals use formal-typed recursive ordinal aliases with read-only input and atomic selected output/inout updates through nested/generated hierarchy; whole packed aggregates, constant member reads/writes, and one-level constant member bit/part-selects with specialization-folded parameter or package-constant bounds execute through common extract/insert/sliced-write SimIR operations; recursive hierarchy with per-instance integral constant/type specialization, declaration-ordered generated-parameter folding, specialization-selected or always-selected generated behavior with scope-qualified locals, loop-variable substitution, and named or positional whole-object or direct static-slice connections | Nested structs/aggregates, unequal-width or tagged unions, unpacked members, member initializers, anonymous structs/enums, interfaces, classes, unsupported task/function forms, and export package items are not implemented; dynamic or recursively chained member selects and nominal struct/union/enum assignment/cast legality are not yet enforced; bounded aliases ultimately resolve to `bit`, `logic`, `reg`, `int`, or `integer`, while enum bases and aggregate members use packed `bit`/`logic`/`reg`; module integer objects use executable signed 32-bit two-state storage; constant/type widths above 64 bits, complete LRM expression typing, genvar-dependent typed constants, and generated type declarations are not implemented; package names resolve within the owning manifest library; no interfaces, binary or positioned file I/O, standard descriptor aliases, multichannel descriptors, general dynamic string allocation/methods, noncanonical generate-loop updates, generated functions/tasks, general expression or runtime-variable/dynamic-container port actuals, multidimensional unpacked arrays, cross-language containers, or aggregate/string-element memories |
| Verilog/SV statements | `assign`, bounded scalar built-in gate primitives, event-controlled `always`/`always_ff`, inferred `always @*`/`always_comb`/`always_latch`, `initial`, SystemVerilog `final`, blocks, leading packed procedural variables, `if`/`else`, `case`/`casez`/`casex` with comma-separated choices and `default`, canonical bounded procedural `for`, locally static `repeat`, runtime `while`/`do-while`, timing-controlled `forever`, nested `break`/`continue`, blocking/NBA assignments, SystemVerilog compound assignments and standalone prefix/postfix increment/decrement, exact decimal/scientific delays, parenthesized `min:typ:max` delays, one/two/three-value continuous-assignment transition delays, one/two-value supported-gate delays, any-change/`posedge`/`negedge`/wildcard procedural controls, condition waits, `$stop`, `$finish`, standalone `$info`/`$warning`/`$error`/`$fatal`, immediate assertions with simple or lexical-block pass/failure actions, the nonsuspending statement subset inside bounded automatic functions, and the supported scheduler controls inside bounded automatic tasks, bounded SystemVerilog-2017 `$fopen`/`$fclose`/`$fdisplay`/`$fwrite`/`$fgets`/`$feof`/`$ferror` text-file forms, and `$readmemb`/`$readmemh` into bounded static memories | Whole, constant bit/part-selected, constant `+:`/`-:` indexed-selected, and dynamic single-bit packed signal/local assignments using the common signed 32-bit index representation; dynamic selected delayed/NBA writes capture their index at assignment execution; SystemVerilog arithmetic, bitwise, logical-shift, and arithmetic-shift compound assignments plus standalone `++`/`--` normalize to blocking read-modify-write behavior; bounded automatic function bodies execute blocking local assignments, blocks, conditionals, exact case, canonical loops, break/continue, nested nonrecursive calls, function-name assignment, and value return; bounded automatic task bodies add statement delays, named-event waits/triggers, and condition waits to the shared control subset with nested function/task calls, valueless early return, persistent formals/locals, and deferred ordered input/output/inout copy-in/copy-out; `$stop` pauses before the following statement and resumes after the application clears the stop; severity tasks retain an optional bounded literal message, with note/warning/error continuing and `$fatal` accepting an optional ignored numeric finish control before terminating; final procedures execute exactly once after ordinary quiescence or `$finish` and may contain the supported nonsuspending blocking statement subset; comma-separated optionally named `buf`/`not`/`and`/`nand`/`or`/`nor`/`xor`/`xnor` primitives with an optional shared selected transition delay lower to independent common four-state continuous processes; inline `int`/`integer` procedural loops with locally static `<`/`<=`/`>`/`>=` bounds, matching unit updates, null ranges, and bounded deterministic unrolling; nonnegative locally static repeat counts; executable pre/post-test loop backedges with nested control transfers; timing-controlled `forever`; nested `if`/`else` with packed four-state truth conversion; deterministic wildcard dependencies; time-zero `always_comb`/`always_latch`; ordered exact and symmetric selector-or-choice wildcard case matching; dynamic event suspension and immediate-test condition waits; packed-condition immediate assertions with implicit error and scoped pass/failure actions; delays inherit the active time context, select `min`/`typ`/`max`, then round to SystemVerilog precision; delayed continuous whole/slice writes select rise/fall/turnoff from actual changed four-state elements, use the shortest applicable packed transition, and cancel superseded inertial updates; manifest-relative text and memory files use opaque process-owned services with deterministic byte/element bounds and interpreter/native lifecycle parity | Gate strengths, static gate-instance arrays, net-declaration/path delays, and MOS/switch primitives are not implemented; `$stop` accepts but ignores its optional verbosity argument; final procedures reject timing controls, waits, `$stop`, `$finish`, and NBAs; wildcard inference does not inspect function/task bodies; no parameterized/nonconstant delays, nonsuspending `forever`, dynamic or negative repeat counts, externally declared or non-unit-step procedural `for` indices, `case inside`, `unique`/`unique0`/`priority` case qualifiers, dynamic part-select assignment targets, nested selected targets/scopes, automatic process variables beyond the substituted loop index, compound-assignment delay controls, increment/decrement within larger expressions, static/ref/default/unpacked tasks or unsupported function forms, fork, general expression controls, binary/positioned/multichannel files and standard descriptor aliases, multidimensional or aggregate/string-element memories, unrestricted dynamic allocation, formatted/dynamic severity-task messages, or procedural force |
| Verilog/SV expressions | Identifiers, sized literals, strings, unary and common binary syntax, conditional (`?:`), index/part-select/concatenation syntax, call syntax | Identifiers/literals, unary plus/minus, bitwise complement (`~`), vector-aware logical negation (`!`), mixed-width logical and/or, unary and/or/xor reductions and their `~&`/`~|`/`~^`/`^~` complements, bitwise and/or/xor plus binary `~^`/`^~` XNOR, logical and arithmetic left/right shifts with signedness-sensitive four-state sign fill, left-associative fixed-width `**`, equal-width signed/unsigned add/subtract/multiply/divide/remainder and relational comparisons, equality/inequality with unknown propagation, exact known-result case equality/inequality (`===`/`!==`), right-operand-masked SystemVerilog wildcard equality/inequality (`==?`/`!=?`), equal-width conditional alternatives under a scalar condition with four-state bit merging, constant in-range bit/part selects, dynamic single-bit selects using the common signed 32-bit index representation, constant `+:`/`-:` indexed part-selects with declared-range mapping, packed concatenations, direct locally constant colon or indexed static-array selections in supported contextual assignment, consumer, callable, ordering, and module-port forms, checked constant replication concatenations of statically sized operands, bounded nonnegative integral `$clog2` constant calls, width/bit-preserving `$signed`/`$unsigned` casts, packed `$isunknown`, `$onehot`, and `$onehot0`, signed 32-bit `$countones` and constant-control `$countbits`, 32-bit `$bits` results for statically sized packed expressions and direct one-dimensional integral unpacked containers, signed 32-bit `$left`/`$right`/`$low`/`$high`/`$size`/`$increment` results with an optional constant dimension `1` over packed objects and supported unpacked containers, packed/unpacked `$dimensions`/`$unpacked_dimensions` counts, direct contextual positional/keyed assignment patterns for supported whole containers including bounded static-array default/index-key patterns, exact-element-type `sum`/`product`/`and`/`or`/`xor` reductions with an optional bounded pure `item`/`item.index` conditional transformation, no-argument `min`/`max`/`unique`/`unique_index` queue locators, and bounded pure-predicate `find`/`find_index`/`find_first`/`find_first_index`/`find_last`/`find_last_index` queue locators on supported direct unpacked containers, and bounded local/imported/package-selected automatic integral function calls in constant and runtime expressions | Dynamic part-selects, streaming concatenations, user-function calls outside the bounded automatic integral subset, dynamic `$countbits` controls, query type references, indirect or multidimensional container queries or reductions, named or arithmetic/function/side-effecting reduction transformations, no-argument locator `with` clauses, type-keyed or nested assignment patterns and defaults outside direct one-dimensional static arrays, predicate iterator calls/side effects/arithmetic, dimension arguments other than `1`, and arbitrary vector-valued/negative `$clog2` arguments are not lowered; out-of-range or unknown dynamic indices fail at runtime; conditional vector truth conversion, expression side effects/short-circuit observation, and full self-determined sizing remain pending |
| Preprocessing/directives | Quoted and angle includes, manifest/CLI definitions, object/function macros with default arguments, multiline replacement, argument substitution, token concatenation/stringification, `__FILE__`/`__LINE__`, `undef`, nested conditional compilation, logical `` `line`` source remapping, legal `` `timescale``, `` `default_nettype``, reset/cell/keyword-version/unconnected-drive state, and ordered `file`/`source-set`/`combined` policies | Included units and macro-selected executable source enter the normal frontend; active `` `line`` mappings reach parser diagnostics, macro ancestry, DesignIR/SimIR debug points, report callbacks, and LLVM objects while physical ownership remains in analysis/native cache provenance; mappings reset for includes and compilation-unit roots; source-set/combined roots otherwise share macro, conditional, and parser directive state while retaining library ownership; scalar implicit nets and default port net types honor `` `default_nettype``; cell metadata and omitted-input pulls reach DesignIR/runtime; time directives and declarations scale exact delays and contribute to `auto` resolution; ordered snapshots participate in cache identity | Standardized pragma behavior, multi-driver wired-net resolution, and complete trireg charge semantics remain incomplete; unsupported directives receive targeted errors |
| SystemC | C++ compatibility header, versioned plug-in entry point, typed factories, and peer mixed-language hierarchy | Common signals/ports/exports/events/channels, native and foreign children, lifecycle callbacks, `SC_METHOD`, and Boost.Context-backed `SC_THREAD`/`SC_CTHREAD` timed/event/static waits execute on the deterministic common kernel | Arbitrary custom-interface metadata, dynamic processes, thread reset/kill, TLM/AMS/CCI, and Accellera ABI compatibility remain unsupported |

SystemVerilog constant-expression status update: supported integral parameter
expressions now use a typed 1–64-bit semantic value rather than host-C++
promotion rules. It retains width, signedness, X/Z masks, unsized status, and
source span for sized, unsized decimal, unsized based, and unbased unsized
literals. The bounded operator set covers unary/reduction operators,
arithmetic, bitwise/logical operators, equality and case equality, relations,
shifts, power, conditional expressions, concatenation, replication,
`$signed`, `$unsigned`, `$isunknown`, and `$clog2`. Defaults, localparams,
named/positional overrides, non-iterative generated constants, packed ranges,
and conditional/case generate choices share this evaluator. Four-state
values remain legal in four-state parameter types and are rejected when a
two-state conversion would lose information. The canonical `svconst-v1`
identity prevents equal display strings with different widths or signedness
from sharing native objects. Widths above 64 bits, complete LRM typing, and
genvar-dependent typed constants inside iterative generate bodies remain
unsupported.

SystemVerilog type-parameter status update: module and package parameter
regions accept bounded `parameter type` and `localparam type` declarations.
Defaults and named/positional actuals may resolve supported integral builtins,
local typedefs, wildcard-imported types, and directly package-selected marks.
Identifier actuals remain tentative until matched to a type formal, while
unambiguous builtin data types retain explicit typed HIR. Per-specialization
aliases flow into dependent packed ports, signals, typedefs, value parameters,
localparams, and value-dependent default ranges; nested same-language
forwarding preserves declaration order and source provenance. Versioned
`sv-type-v1` identities participate in native-object cache keys.
Unpacked/interface/class/anonymous composite actuals, widths above 64 bits,
generated type declarations, and mixed-language type-parameter transfer
remain unsupported.

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
interpreter and native LLVM O0/O2. The byte-oriented subset includes literals,
empty initialization, blocking value-copy assignment, concatenation,
equality/inequality, byte indexing and replacement, `len()`, and `%s` output.
String locals survive suspending automatic tasks and remain visible at
debugger safe points; module strings support escaped `show` and bounded
`deposit`, while `force` is rejected. Values are limited to 4,096 bytes and
string operation/layout/provenance semantics participate in versioned
native-cache identity. Unicode code-point semantics, general string methods,
and cross-language string boundaries remain unsupported.

SystemVerilog container status update: one-dimensional integral dynamic arrays
`[]`, unbounded queues `[$]`, bounded queues `[$:N]`, integral-key
associative arrays, and locally constant static unpacked arrays `[left:right]`
execute as distinct module objects and automatic block/function/task values.
The bounded subset supports whole-value copy, element reads and writes, and
`size()`; dynamic, queue, and associative containers initialize empty and
support `delete()`, while fixed arrays materialize 1–4,096 elements in declared
index order with bit-zero or four-state-X defaults.
Dynamic arrays add `new[size]`; queues add `push_front`, `push_back`,
`pop_front`, and `pop_back`; associative arrays add `exists(index)`,
`delete(index)`, and `first`/`last`/`next`/`prev` traversal. Values preserve
exact element width, signedness, and two-/four-state domain. Associative keys
add the same exact type metadata, require known values, and remain in canonical
numeric order; a missing-key read returns the element type's zero default
without inserting. A full bounded queue discards its back element after
insertion, and every container is limited to 4,096 elements or entries.
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
Associative conditional values and mutating methods on temporary results remain
unsupported. Schema 47 and container semantic revision 23 cover result kinds,
ranges/bounds, element/index profiles, consumer and conditional operations,
specialization, and transitive source provenance; normalized equivalent values
share native cache identity without a public ABI change.
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
suspension. Arbitrary value arguments, keys on `reverse`, associative arrays,
indirect or read-only receivers, arithmetic/calls/side effects in keys,
expression-result use, and nondeterministic `shuffle()` remain unsupported.
Direct nonassociative static arrays, dynamic arrays, queues, and bounded
queues also support `min()`, `max()`, `unique()`, and `unique_index()` with
an optional parenthesized `with` transformation when their result is assigned
to a compatible queue.
Value results preserve the exact element profile; index results use signed
two-state 32-bit elements. Empty sources produce empty results, extrema return
one first-occurring value, uniqueness preserves first occurrences by
four-state identity, and unique indices use signed declared static indices or
current dynamic/queue indices. Results remain bounded to 4,096 elements or the
destination queue capacity, and aliased queue assignment evaluates the source
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
common empty, destination-capacity, 4,096-element, object/port/callable,
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

VHDL array-aggregate status update: constrained one-dimensional
scalar-element user arrays now accept contextually typed positional,
locally-static discrete/range/choice-list, and final `others` associations in
initializers, whole assignments, conditional alternatives, and equality
expressions. Choices follow the declared ascending or descending ordinal
range and may fold visible package constants or prior generics. The compact
table's older blanket references to unsupported “array aggregates” and
“choice-list aggregates” are therefore superseded for this bounded form;
nested, multidimensional, qualified, composite-element, and dynamically
chosen aggregates remain unsupported.

VHDL array-attribute status update: concrete user-array objects and visible
type/subtype marks support `left`, `right`, `low`, `high`, `length`, and
`ascending`, with optional dimension `1`. Sequential loops accept `range` and
`reverse_range` as complete discrete ranges and preserve declared direction,
including bounded `next` and `exit` behavior. These static scalar attributes
also participate in array indices, slice bounds, and aggregate choices.
Unconstrained marks, dimensions other than `1`, and scalar use of a range
attribute are diagnosed.

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
and retain recursive configurations for existing unguarded labeled blocks and
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
New generic families, nested package/template forms, nested or otherwise
unsupported composite interfaces, general expression/aggregate port actuals,
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
`repeat (N) @event` NBA controls, general event expressions, and
parameterized/nonconstant controls remain deferred.

Named-event status update: Verilog-2005/SystemVerilog module-level `event`
declarations, comma groups, immediate `->` triggers, static `@event`, dynamic
`@(event)`, and repeated wakeups now execute. SystemVerilog `->>` publishes
through the common update phase and `->> #delay` publishes at a future
timestamp; event arguments and general event expressions remain deferred. This update
supersedes the older broad “named events” limitation in the compact table.

SystemVerilog function status update: module and package functions with an
explicit `automatic` lifetime, bounded integral, byte-string, supported
container, or one-dimensional locally constant static-array value types,
ANSI value-input arguments, or a classic no-argument header now execute.
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
the caller's whole array. Static or implicit lifetimes, classic body argument
declarations, output/inout/ref/default formals, multidimensional results,
widths above 64 bits, recursion, timing/event/task statements, non-byte-string
types, nonstatic slicing, general container-valued return expressions, DPI,
and generated functions remain deferred.

SystemVerilog task status update: module and package tasks with an explicit
`automatic` lifetime, bounded integral, byte-string, supported container, or
one-dimensional locally constant static-array input/output/inout formals,
ANSI arguments, or a classic no-argument header now execute. Calls resolve
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
Static or implicit lifetimes, classic body arguments, `ref`, default formals,
other unpacked arguments, widths above 64 bits, recursion, non-byte-string
types, fork/join and general event expressions, runtime-variable or
dynamic-container module-port actuals, DPI, generated tasks, and
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
vector literals retain `U`, `X`, `0`, `1`, `Z`, `W`, `L`, `H`, and `-`
through common signals, locals, structural operations, projected transactions,
signal last-value state, standard logical operators, equality, and
`std_logic` resolution in both the reference engine and generated LLVM O0/O2
code for supported values up to 64 elements. Generated code uses appended
pointer-based four-plane callbacks and explicit Logic9/Logic4 conversions;
wide-value runtime kernels remain incomplete.

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
aggregates/interfaces, or wired-net resolution beyond `sv_wire`. Exact
nine-state `std_logic` and four-state `sv_wire` resolution use process-owned
driver slots across explicit mixed bindings. At a VHDL/SV boundary the owning
signal domain is retained and the reader/writer view applies the documented
ordinal per-element conversion in either hierarchy direction. SystemC
factories already elaborate as peer hierarchy nodes in either direction
through explicit bindings.

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
