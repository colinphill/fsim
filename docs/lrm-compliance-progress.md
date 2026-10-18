<!-- SPDX-License-Identifier: Apache-2.0 -->
# LRM compliance progress

Goal (owner, 2026-10-08): close every language-compliance gap needed to run
the public compliance corpora unmodified, apart from simulator-specific hooks.
Design notes: [lrm-compliance-notes.md](lrm-compliance-notes.md).

## Corpora and runner

The corpora are kept outside the repository, under
`.local-artifacts/lrm-corpora/sources`:

| Suite | Source | License | Pass/fail convention |
|---|---|---|---|
| sv-tests | chipsalliance/sv-tests | ISC | exit status against `:should_fail_because:`, plus each `:assert:` line |
| verilator | Verilator `test_regress` 'simulator' scenario tests | CC0 test files | generated clock shell; `*-* All Finished *-*` |
| ivtest | Icarus `ivtest` regress lists | GPL-2.0 | `PASSED` line or gold output; CE/RE expect errors |
| vests | GHDL `testsuite/vests` (VHDL-93) | GPL-2.0 | no error-severity assertion; output files compared |
| nvc | nvc `test/regress` | GPL-3.0 | normal/fail/gold kinds |
| vhdl-compliance | VHDL/Compliance-Tests (VUnit) | Apache-2.0 | per-test results through the VUnit stand-in |

UVM-tagged sv-tests compile the Accellera UVM trees named in
[uvm-source-provenance.md](uvm-source-provenance.md), under
`.local-artifacts/lrm-corpora/uvm`. The runner compiles each release's
`uvm_pkg.sv` once per run, into a library `uvm` under `OUT/shared/`. Each UVM
case maps that library (`fsim library map uvm DIR`) and compiles and
elaborates with `--search-library uvm`, instead of compiling `uvm_pkg.sv`
with every test.

Run the corpora with:

    python3 scripts/lrm_corpus.py run [--suite S] [--filter RE] -j 10 --out DIR
    python3 scripts/lrm_corpus.py compare BEFORE/results.jsonl AFTER/results.jsonl

The runner supplies only simulator-specific hooks:

- top selection;
- generic overrides;
- plusargs;
- the Verilator clock shell;
- `scripts/lrm_corpus/vunit_shim.vhd`, which stands in for the VUnit library.

## Baseline (2026-10-08, interpreter)

Harness revision before the UVM, assert-evaluator and Verilator-shell fixes.
VESTs was interrupted by the parser hang described below.

| Suite | Cases | Pass | Fail | Timeout | Skip | Pass rate |
|---|---:|---:|---:|---:|---:|---:|
| sv-tests | 1497 | 908 | 584 | 5 | 0 | 60.7% |
| verilator | 2415 | 172 | 1941 | 6 | 296 | 8.1% |
| ivtest | 2826 | 1170 | 1628 | 3 | 25 | 41.8% |
| vests (partial) | 2406 | 831 | 1409 | 166 | 0 | 34.5% |

## Fix log

### Batch 1

SystemVerilog and Verilog:

- `$time`, `$stime` and `$realtime` failed with error 10 in any unit without
  a timescale. They now use the project resolution, as the unit's delays do.
  - Before: 386 Verilator and 50 ivtest cases failed this way.
- A whole-signal port actual of a different packed width was rejected. It
  now connects through an adapter that zero-extends or truncates, as a
  continuous assignment does (IEEE 1800-2017 §23.3.3).
  - Before: 278 ivtest cases failed this way.
- `` `begin_keywords``, `` `end_keywords`` and `` `pragma`` are accepted in
  Verilog-2005 (IEEE 1364-2005 §19.10–19.11).
- The scalar `Convert` operation can return an integral result (used by
  VHDL `integer(r)`, rounding to nearest, ties away from zero).

VHDL types:

- Integer and floating type declarations (`type t is range ...`) sent the
  parser into an endless error loop until memory ran out.
  - They now declare INTEGER- or REAL-based types; the physical-unit loop
    recovers from malformed units.
  - Before: VESTs compile timeouts.
- Arrays may be indexed by any discrete type mark: `array (E)`,
  `array (E range <>)` and `array (E range L to R)`. Aggregates accept BOOLEAN
  and character-literal choices.
- REAL:
  - Arithmetic, comparison, negation and `abs` ran on raw bit patterns. They
    now use the IEEE real scalar operations.
  - `real` signals failed to elaborate.
  - `integer(r)`/`real(i)` were rejected.
  - Real-literal actuals could not match REAL formals in overload
    resolution.

VHDL strings, subprograms and STD.ENV:

- Runtime `T'IMAGE(X)` was missing entirely (`report integer'image(n)`
  failed). So were `TO_STRING`/`TO_HSTRING`/`TO_OSTRING` of runtime values.
  - Now implemented for integer, enumeration, BOOLEAN, BIT, STD_ULOGIC,
    CHARACTER, REAL, and bit or logic vectors.
  - TIME images are still pending.
- Subprograms:
  - Functions may contain `report`/`assert`.
  - Procedures may contain `report`/`assert` and signal assignments.
  - Formals and results may be constrained arrays, TIME or REAL.
  - Procedures accept signal-class formals, which denote the actual signal.
- Procedures declared in a process can read and write the process's
  variables.
- Wrong-result fix: a procedure's frame captured the first call's constant
  actuals, so `p(3); p(5)` ran `p(3)` twice.
- STRING procedure formals (mode `in`), with string and real formals taking
  part in procedure overload resolution.
- `now` (STD.STANDARD.NOW) was not implemented. It now returns the current
  time.
- The final `else` of a conditional signal assignment is optional.
- Loops over a type mark (`for d in T loop`, `for i in T range L to R loop`)
  are supported. Generates also accept a type mark or `x'range`.
- `std.env.stop`, `finish` and `resolution_limit` are accepted in VHDL-2008.
  - Still needed: the VHDL-2008 `use std.env.all` form, which requires a
    VHDL-2008 STD.ENV package.

Regression fixtures: `tests/fixtures/compliance/`. They run on both engines,
and their expected values were checked against xsim.

### Corpus run 2 (after batch 1, interpreter)

| Suite | Cases | Pass | Fail | Timeout | Skip | Pass rate |
|---|---:|---:|---:|---:|---:|---:|
| sv-tests | 1497 | 988 | 504 | 5 | 0 | 66.0% |
| verilator | 2415 | 397 | 1715 | 7 | 296 | 18.7% |
| ivtest | 2826 | 1198 | 1600 | 3 | 25 | 42.8% |
| vests | 3665 | 1710 | 1953 | 2 | 0 | 46.7% |
| nvc | 1482 | 412 | 852 | 3 | 215 | 32.5% |
| vhdl-compliance | 72 | 0 | 72 | 0 | 0 | 0.0% |

### Batch 2

- A protected type body following its declaration in the same region was
  reported as a duplicate type.
- `fork ... join_none` in functions is accepted from SystemVerilog-2009
  (IEEE 1800-2009 13.4.4); lowering in subprogram bodies is pending.
- VHDL ports of every mode accept a default; using it as the driver's
  initial value is pending.
- Enumeration values index arrays (validation).
- `rising_edge`/`falling_edge` lower as boolean expressions, including on
  signal-class formals and with `else` branches.
- `wait for` accepts a runtime TIME value.
- Functions called without an actual parameter part (`return do_it;`).
- SystemVerilog hierarchical references resolve below the current instance,
  below each ancestor, from the top-level modules, and through `$root`, for
  both reads and writes. A process naming an instance that does not exist yet
  is lowered again after the unit's children are instantiated, and such
  processes are never cached as templates. Still pending: event controls on
  hierarchical names, continuous assignments, and generate-scope processes.
- `fork ... join_none` in functions also lowers from SystemVerilog-2009.
- FSIM-ELAB-HIR-001 "could not be lowered" reports now name the innermost
  unsupported expression; `lrm_corpus.py run --rerun-from --rerun-code`
  re-runs selected failures.

### Batch 3: enumeration-indexed arrays and composite constants

- Signals with composite initializers failed to elaborate unless the elements
  were single logic values. This covered arrays of vectors, integers,
  enumerations and records, multidimensional arrays, enumeration indices,
  and range or OTHERS choices. They are now evaluated statically.
- Composite constants whose elements are not single logic values could not
  be read in processes (`constant rom : rom_t := ...; x := rom(i);` failed).
  Constants in architectures are materialized as read-only signals. Constants
  in non-generic packages are materialized on first use, so dynamic indexing
  works too.
- Enumeration-typed index expressions (`arr(sel)` with `sel` an enumeration
  signal or variable) are accepted for reads and writes, including record
  fields of selected elements and multidimensional selections.
- BOOLEAN, BIT, CHARACTER, SEVERITY_LEVEL and STD_ULOGIC type marks give
  static index ranges (`array (e_t, boolean) of natural`).
- `T'POS(d)` and other enumeration attributes accept a for-loop parameter
  that iterates over `T`, either directly or through an array's
  `'RANGE`/`'REVERSE_RANGE`.
- Based and exponent integer literals (`16#A0#`, `1E3`) lower at run time.

Fixtures: `vhdl_enum_index_arrays.vhd`, `vhdl_composite_constants.vhd`.
Both were checked against xsim.

### Batch 4: range constraints, interface ranges, re-analysis

VHDL:

- Index and range constraints accept range attributes (`x'range`,
  `x'reverse_range`), discrete subtype names (`mem_t(idx_t)`) and
  `T range L to R`. The direction follows the evaluated bounds.
  - Before: 125 nvc/VESTs cases failed with `FSIM-VHDL-PARSE-009`.
- Array attributes of unconstrained subprogram formals and ports
  (`vec'range`, `vec'length`) were rejected outright. Locals sized from them
  (`variable r : bit_vector(vec'range)`) now get their bounds per call frame.
- An element subtype constrained with non-literal bounds
  (`bit_vector(0 to N-1)`) is no longer treated as unconstrained and gated to
  VHDL-2008.
- Re-analysis replaces a library unit (IEEE 1076-1993 11.4, 1076-2008 13.5),
  even when another design file defined it.
  - The secondary units of a replaced primary unit, and catalog objects that
    depend on a replaced unit, become obsolete.
  - Before: 104 VESTs cases failed with `FSIM-SEM-0003` or `FSIM-WS-001`
    unit collisions.
- The reserved word `component` may precede the unit name in a component
  instantiation (VHDL-93 9.6).
- Incorrect VHDL-2008 gates removed:
  - A function-call result may be indexed, sliced or element-selected in
    every revision (VHDL-93 6.1, 6.3).
  - The names `boolean_vector`, `integer_vector`, `real_vector` and
    `time_vector` are free for user types before VHDL-2008.
  - Before: 84 VESTs cases failed with `FSIM-FE-VHSTD-003`.

SystemVerilog:

- A streaming concatenation is accepted as an assignment target (unpack) from
  SystemVerilog-2005. It had been gated to 2023.

Test harness: a compliance fixture header may name `before=<file>`, a source
compiled into the same library first. Fixtures:

- `vhdl_range_constraints.vhd`
- `vhdl_reanalysis.vhd` (with `vhdl_reanalysis_first.vhd.in`)
- `sv_streaming_unpack.sv`

All were checked against xsim.

Accepting these constraint forms exposes missing analysis-time legality
checks in about 33 VESTs negative tests. They had been rejected only by the
constraint parse error:

- index-constraint bounds outside the index subtype;
- slices of multidimensional arrays;
- labels and other non-object names used as primaries.

### Batch 5: compilation-unit declarations

- Compilation-unit (`$unit`) parameters, local parameters, typedefs,
  variables, `let`, events, functions and tasks (IEEE 1800-2017 3.12.1) are
  collected in a synthetic package per design file (`fsim_unit_<hash>`).
  Later design units of the compilation unit import it.
  - Before, `typedef` and `let` there were silently skipped, functions were
    rejected as unqualified class methods, and parameters and variables
    failed with `FSIM-SV-UNSUPPORTED-001` (218 cases).
- Forward type declarations (`typedef struct name;`, `typedef name;`,
  IEEE 1800-2017 6.18).
- `rand`/`randc` members of unpacked structures parse. An aggregate member
  the parser cannot read no longer loops forever.
- Member selection (`p.hi`) on a variable whose structure type comes from
  another unit, such as a package typedef or `$unit`, now resolves. Such
  type references keep only their spelling across object linking.

Fixture: `sv_compilation_unit_declarations.sv`, checked against xsim.
Targeted rerun of the 218 cases: 63 now pass. Most of the rest fail later
on other gaps:

- package parameter defaults (`FSIM-ELAB-SVPKG-006`);
- non-ANSI port forms;
- randomization.

### Batch 66: protected objects, global constants, per-element resolution, Verilog net rules

Closes corpus cases from the Batch 66 triage indexes
(`.local-artifacts/lrm-closure/triage/b66-INDEX.md`, `b66-protected.md`,
`b66-nvc-hir001.md`, `b66-verilator-hir001.md`).

VHDL:

- Protected types are lowered as objects: each shared, process or nested
  object is an anchor whose private variables are created on first use and
  initialized lazily; methods are ordinary callables with a frame per object,
  overloaded through normal resolution, and may call other methods, pass
  protected formals, and declare body-local constants, types, aliases and
  files (5.6.2, 6.5.2). This replaces the bounded inline path.
- Constants outside processes whose values need run time (function results,
  composites) are elaborated by their first reader and shared (6.4.2.2);
  architecture and package file objects share one handle opened by their
  first user (6.4.2.5).
- A composite whose element subtype is resolved resolves each scalar
  subelement on its own (14.7.3.2); resolution formals indexed from POSITIVE
  start at 1.
- Character and boolean literals take their type from context: a literal of
  a user enumeration in a comparison, assignment, case choice, initializer or
  record aggregate is that enumeration's literal (5.2.2.1); `'POS` of
  TRUE/FALSE redeclared by a user type.
- `??` returns BOOLEAN and is TRUE only for '1' and 'H' (9.2.9);
  `std_logic'(...)` qualification; implicit condition operator.
- Constant evaluation: REAL expressions and MATH_REAL functions, INTEGER(x)
  of a REAL, `C.e` of a record constant and `C(i)` of an array constant with
  aggregate values, TIME'LEFT/'HIGH, physical values times or divided by a
  REAL (9.2.7), lazily evaluated generate constants of type STRING.
- Static rules: initial values of objects of a ranged integer or physical
  type are checked against the type's range at analysis (5.2.1); a unit name
  is hidden by an object of the same name (12.3); sensitivity lists accept
  names indexed by globally static expressions (8.1); the physical unit
  position check is retired (5.2.4.1 allows units outside the range).
- Recursive instantiation through generate statements with changing generic
  values (14.5.3); NUMERIC_STD MINIMUM/MAXIMUM of UNSIGNED/SIGNED (G.2);
  STRING aggregates with named and OTHERS choices; CHARACTER control-name
  indexes (NUL, SOH, ...); BOOLEAN loop parameters in conditions; record
  elements of ranged REAL subtypes default to the range's left bound; BIT
  literal expressions as two-state aggregate elements; an unbound component
  instance's out ports drive their defaults (14.7.3.2).

SystemVerilog and Verilog:

- `std::randomize` accepts any writable packed lvalue (class properties,
  struct members, unpacked and class-array elements); with-clauses name the
  same lvalues and read calling-scope values when the call executes (18.12).
- Verilog (IEEE 1364-2005) rejects a variable as a continuous-assignment
  target (6.1) and as an output or inout port actual (12.3.9.2), new
  FSIM-ELAB-SVPORT-013; SystemVerilog keeps its relaxations.
- A function may return an anonymous enumeration, whose literals join the
  enclosing scope (6.19, 13.4); a macro's formal argument list may span lines
  (22.5.1).
- Runner: VUnit shim check_passed/check_failed; nvc `--shuffle` cases and
  Verilator `$frewind` cases are out of scope (non-IEEE); SDF files without
  SDFVERSION are out of scope (IEEE 1497-2001 5.2).

Measurement: all six suites were run in full on frozen `b66w10` and compared
with Batch 65 (`b65w8`): **51 new passes, zero regressions**. Batch 65 is
shown on the same in-scope set; none of the newly excluded cases passed
before.

| Suite | Batch65 passes / in scope | Batch66 passes / in scope | Excluded |
|---|---:|---:|---:|
| ivtest | 2511 / 2570 | 2514 / 2570 | 256 |
| sv-tests | 1434 / 1497 | 1434 / 1497 | 0 |
| verilator | 1539 / 2137 | 1541 / 2137 | 278 |
| vests | 3559 / 3646 | 3578 / 3646 | 2 |
| nvc | 852 / 1261 | 877 / 1261 | 221 |
| vhdl-compliance | 39 / 72 | 41 / 72 | 0 |

The labelled ctest run ends with the same 27 known failures. Tests that
encoded non-LRM behaviour were changed to the LRM rule: protected objects
are initialized on first use and may call procedures (5.6.2), a module
variable is a legal std::randomize argument (18.12), and a null slice's
bounds need not lie in the index range (8.5). The physical-unit fixture moved
from the analysis-error set to the passing compliance fixtures.
FSIM-ELAB-VHPROTECTED-012 to -016, -018, -019 and -021 are retired and
FSIM-ELAB-SVPORT-013 added (2965 codes).

Deferred (recorded in `.local-artifacts/lrm-closure/triage/b66-INDEX.md`):
port conversion functions at initialization (nvc conv*), global constraints
through rand class handles (planned for Batch 67), port aliasing across
hierarchy levels, context-typed character literals for enumeration subtype
bounds and defaults (tc1724), STD_ULOGIC type attributes, signature aliases
of procedures, record values in the constant evaluator, implicit-signal
attributes of attributes, and procedural concurrent assertions.

### Batch 65: nested packages, generic subprograms, inline constraint scope, loop ranges

Continues the unified architecture plan (`.local-artifacts/lrm-closure/ARCH-PLAN-b64.md`,
steps 11 and 13) and closes corpus cases from the Batch 65 triage index
(`.local-artifacts/lrm-closure/triage/b65-c/INDEX.md`).

VHDL:

- Nested package declarations and bodies (VHDL-2008, 4.7) are analyzed as
  units of their own, made visible through the enclosing package; expanded
  type marks `lib.pkg.t` select the package at elaboration.
- Subprogram instances are overloadable (4.4); generic subprograms conform by
  scope-aware profiles; `is <>` subprogram defaults bind with earlier generics
  of the same list; an operator box default with no declared match is the
  predefined operation (6.5.6.2).
- Sequential `for` loops over runtime enumeration or BOOLEAN bounds, and over
  integer bounds whose operator tree reports no signedness (10.10). A loop
  range with a CHARACTER bound makes a `'0'`-like bound a CHARACTER too
  (5.3.2.1), so `for I in '0' to '9'` indexes a CHARACTER-indexed array.
- A concurrent procedure call may call a procedure that waits (11.4).
- Predefined MINIMUM/MAXIMUM have result subtypes, and an array of
  enumeration elements (BIT, BOOLEAN) compares element positions unsigned
  (5.3.2.4).
- `'element`, record aggregates of CHARACTER elements, dynamic element
  constant slices, null-array assignment and comparison, REAL `to_string`
  formats, dotted signal dependencies.

SystemVerilog and Verilog:

- `randomize() with { ... }` inside a class method reads the method's
  formals and locals: names the object does not declare take their values
  from the calling scope (18.7).
- A Verilog-2005 function must declare an input (IEEE 1364-2005 10.4.1,
  new FSIM-SV-SEM-422); SystemVerilog still allows none.
- An unsized x/z enum value that does not fit the base type is an error
  (6.19). A wildcard export carries only names the package refers to
  (26.3, 26.6); package constants, which the frontend folds at their uses,
  stay exported.
- Interface ports inherit interface types in port lists; forwarded type
  parameters; struct/union class properties; `:final` and override
  specifiers; classic task/function headers with implicit formals; stream
  unpacking into wider targets; clocking-block events `@(cb)`; block-scope
  enum literals and parameters; zero distribution weights; negative real
  delays wrap like their integral value; task calls on generate-block
  array elements (`blk[1].task`).
- Out-of-scope lists gained Verilator and Icarus cases that need non-LRM
  extensions (excluded counts below).

Measurement: all six suites were run in full on frozen `b65w8` and compared
with Batch 64 (`b64d`): **50 new passes, zero regressions**. Batch 64 is
shown on the same in-scope set; none of the newly excluded cases passed
before.

| Suite | Batch64 passes / in scope | Batch65 passes / in scope | Excluded |
|---|---:|---:|---:|
| ivtest | 2502 / 2575 | 2511 / 2575 | 251 |
| sv-tests | 1434 / 1497 | 1434 / 1497 | 0 |
| verilator | 1516 / 2140 | 1539 / 2140 | 275 |
| vests | 3556 / 3646 | 3559 / 3646 | 2 |
| nvc | 837 / 1262 | 852 / 1262 | 220 |
| vhdl-compliance | 39 / 72 | 39 / 72 | 0 |

The labelled ctest run ends with the same 27 known failures. Tests that
encoded non-LRM behaviour were changed to the LRM rule: header-only classic
task arguments (frontend), a negative delay wraps (suspending tasks), a
no-input Verilog-2005 function is rejected (frontend, 1364-2005 10.4.1), and
wildcard exports in the resolver, conformance and interface tests now refer
to the names they export (26.6). FSIM-VHDL-SEM-062 is retired and
FSIM-SV-SEM-422 added (2972 codes).

Deferred (recorded in the plan): VHDL character literals overloaded by a
visible user enumeration resolve to the user literal in BIT/std_logic
contexts (needs context-typed literal resolution; tc870-873), class property
widths from class value parameters in lowered methods, slices by a range
attribute (`x(x'range)`), BOOLEAN loop parameters in conditions (tc1019),
and overload resolution with null-range array actuals.

### Batch 64: unified architecture (value paths, bit streams, aggregate shapes), class value parameters

Four read-only triage lanes (sv-tests, Verilator, nvc, VESTs and compliance)
extended the index (`.local-artifacts/lrm-closure/triage/b64-INDEX.md`).

SystemVerilog and Verilog:

- Class value parameters (8.25): a declaration `C #(7) h` selects its
  specialization, `new` allocates it, and methods that read a class value
  parameter read the object's specialization instead of being inlined with the
  default. A class-body type parameter is a type alias (8.25).
- `$urandom(seed)` seeds the calling process's generator and then draws
  (18.13.1). `x inside {array}` matches the elements of a fixed, dynamic or
  queue array, also of strings (11.4.13).
- Packed arrays of named types: `pkg::t [3:0] v`, `enum_t [3:0]`,
  `struct packed {...} [15:0]` (also with parameter-dependent member widths),
  `if0.rq_t [1:0]` and `foo_t [1:0]` as a type-parameter default (7.4.1). A
  packed array with parameter-dependent dimensions flattens to a range
  expression folded at elaboration, so `x[i].m` works on it.
- An anonymous enumeration as a structure member declares its literals in the
  enclosing scope (7.2, 6.19).
- `$bits(v)'(x)`: a constant function call as a casting size (6.24.1).
- A string literal or packed value converted to `string` drops `"\0"`
  characters (6.16); `string'(x)` no longer shows them as spaces.
- A packed range bound is a self-determined constant expression: sized
  operands keep their widths (`[4'b1111 + 4'b0001 >> 1:0]` is one bit,
  11.6.1). An index of a multidimensional packed array in a folded constant
  condition selects an element, not a bit (7.4.5).
- A parameter whose value calls a function with a defaulted argument that
  reads a variable is not a constant (13.5.3, 6.20.2).
- An element of a real array in another instance (`u.r[2]`) is real.
- SDF: `$sdf_annotate` from a nested module annotates below that instance
  (IEEE 1800-2017 32.4); an empty `()` delay keeps the transition's delay
  (IEEE 1497-2001 5.4.1); a process that assigns one region in several places
  owns it once.
- Unrolled loops no longer elaborate in quadratic time (region-graph width
  tracking), which brought `t_randcase` from over 60 s to about 1 s.

VHDL:

- `S'STABLE` and `S'QUIET` without a time argument wake a waiting process when
  they return to TRUE one delta after the event (16.2.3).
- A component declared in a package can be instantiated without maps
  (`u1 : comp1;`); scalar generics of a component left unassociated take the
  component's default when a binding indication's generic map names them.
- Earlier in the batch: `elsif`/alternative labels in generate statements,
  `file_close` of an unopened file, `rising_edge` from `U`, signed
  `'delayed`, a `wait for` beyond the time range never wakes, implicit `??` in
  VHDL-2008, entity-declared attributes in architectures, `use pkg."+"`.

Unified architecture (owner direction): six read-only triage lanes collected
the deferred large fixes into one plan of five shared mechanisms
(`.local-artifacts/lrm-closure/ARCH-PLAN-b64.md`, index
`.local-artifacts/lrm-closure/triage/b65-INDEX.md`), with an audit of which
earlier one-off fixes each mechanism replaces. Phases 0 to 2 are in this batch;
phases 3 and 4 (package instances and nested packages, activation frames and
change notification, protected types as regions, class methods in SimIR)
continue in the next batches.

Phase 0, corrections and independent fixes:

- The Batch 64 removal of the PKG-001 check for `use lib.unit` was wrong; it
  is restored (tc1658 stays open: the elaborated object selection does not yet
  follow a use clause to an entity).
- SystemVerilog: an untyped parameter with a string default takes the type of
  an override (6.20.2); `localparam type` is not a positional formal (23.10.1);
  real to wider-than-64-bit integral conversion (6.12.2); a recursive module
  instance is keyed by its actuals, with a depth limit (23.3.1); unpacked
  dimensions after a virtual interface name and interface types in typedefs;
  a checker used before its declaration in the same module; specify paths of
  UDP outputs.
- VHDL: postponed processes run at the end of the time slot (14.7.5.3), also
  for postponed concurrent statements; the units of STD TIME are recognised by
  declaration, not by name (tc285, tc286).

Phase 1, VHDL array shapes and lengths:

- A named array aggregate without `others` takes its bounds from its smallest
  and largest choices; only the direction comes from a context that fixes it,
  and a relational operand is not such a context (9.3.3.3). One choice-bounds
  helper serves the lowering, the comparison widths and the validator; a
  choice must lie in the context range only with `others`. The earlier
  descending `count-1 downto 0` materialisation and the single `'range` choice
  special case are gone.
- Port association and component/entity port conformance compare lengths, not
  bounds or direction (6.5.7.1, 7.3.3); BIND-031 now reports only a length or
  rank mismatch. Tests that required equal bounds were flipped.
- An aggregate operand of `&` has the type of the other operand, of the
  concatenation, or of the formal or result it is passed to (9.2.5).
- A sole non-static range choice (`(i*8+7 downto i*8 => '1')`) fills the
  context, with a run-time length check.
- Nested aggregate assignment targets `((a, b), c) := r` and aggregate targets
  of selected assignments (10.5.2.1); unassociated composite generics take
  their default as the actual; deferred constants resolve to their package
  body value; a nested subprogram sees its enclosing subprogram's parameter
  values; null-array record elements; `'last_event`/`'last_active` of slices;
  an open unconstrained block port takes its bounds from its default (6.5.6.3).

Phase 2, structured references and bit streams (SystemVerilog):

- Unpacked values are addressed by a value path built from the HIR structure:
  a container object, then index and member steps, then optionally a packed
  member (`str.el[1].val.next`, `buffer[i]`, `s.pt.b`). One reader and one
  writer walk any path; nonblocking writes of packed members are published in
  the NBA region (10.4.2). Element reads through an unknown or out-of-range
  index give the element default (7.4.6).
- An unpacked structure held by a class property converts to and from a
  module variable of the same type (7.2).
- Assignment patterns with string members, array members and nested container
  elements (`'{'{1, 2}, '{3}}`, 10.9).
- Streaming over unpacked arrays, queues and structures (11.4.14), as source,
  as target (several targets consumed in order) and through bit-stream casts
  to array types (6.24.3); `$bits` of a fixed unpacked array counts all
  elements (20.6.2). The FSIM-ELAB-SVEXPR-003 restriction is removed. These
  run in the interpreter; the native code generator leaves such modules
  interpreted.
- Interface array ports connected to a slice `a[l:r]`, `a[b+:w]` and to
  `a[i].modport` (23.3.3.5, 25.5).
- Tagged unions whose members are unpacked structures, and
  `casez`/`casex ... matches` (12.6.1); FSIM-SV-PARSE-188 is retired.
- VHDL external names: assignment and force targets, absolute `.`, up-level
  `^.` and literal generate-index paths, relative paths from the enclosing
  instance; a process that uses an external name is elaborated after the
  instances it can name (8.7, 14.5).
- VHDL generics: an operator `is <>` default denotes the predefined operator
  when no user declaration matches (6.5.6.2).

Measurement: all six suites were run in full on frozen `b64d` (the full
checkpoint due by Batch 65) and compared with the Batch 63 results; the
pre-architecture binary `b64a` was also run in full as the baseline for the
plan's phases. Against Batch 63: **101 new passes, zero regressions**. One
of them, sv-tests 11.4.14.3 `unpack_stream_pad-sim`, already passed before
this batch; its Batch 63 record was stale. tc1658 (VESTs) passed on `b64a`
only through the wrong PKG-001 removal and fails again, as in Batch 63.
Excluded counts are unchanged.

| Suite | Batch63 passes / in scope | Batch64 passes / in scope | Excluded |
|---|---:|---:|---:|
| ivtest | 2487 / 2630 | 2502 / 2630 | 196 |
| sv-tests | 1426 / 1497 | 1434 / 1497 | 0 |
| verilator | 1472 / 2218 | 1516 / 2218 | 197 |
| vests | 3547 / 3646 | 3556 / 3646 | 2 |
| nvc | 813 / 1262 | 837 / 1262 | 220 |
| vhdl-compliance | 38 / 72 | 39 / 72 | 0 |

The labelled ctest run (`vhdl|systemverilog|compliance`) ends with the same
27 known failures as Batch 63. Tests that encoded behaviour the LRM does not
require were changed to the LRM rule: port and component-port bounds
(vhdl_array_application), named-aggregate length checks (aggregate choice and
type tests now expect FSIM-ELAB-VHARRAYAGG-006), a queue streamed into a
packed target (elaborator_selection), `casez ... matches` (frontend
statements), an absolute external-name path, case-generate labels, implicit
`??` in VHDL-2008 conditions, `$urandom(seed)` and `inside` over arrays.
The diagnostic catalog drops FSIM-ELAB-SVEXPR-003 and FSIM-SV-PARSE-188
(2972 codes).

### Batch 63: VHDL femtosecond resolution, update targets and specify values

Four new read-only triage lanes (Verilator lowering and run-time failures,
ivtest and nvc) extended the index (`.local-artifacts/lrm-closure/triage/b63-INDEX.md`).

Time resolution (owner decision): with `time_resolution = auto`, a design that
contains VHDL units outside STD and IEEE now counts time in femtoseconds, the
primary unit of TIME (IEEE 1076-2008 5.2.4.2). Before, automatic selection
started at 1 ns and refined only to the finest unit spelled in the design, so
`0.01 ns` could not be represented. SystemVerilog-only designs keep the
existing selection. TextIO READ of a TIME value now reads the abstract literal
and its unit; it used to read only the number, which matched only at 1 ns
resolution.

SystemVerilog and Verilog:

- `$timeunit` and `$timeprecision` (20.4.2); `$countbits` with run-time control
  bits (20.9); string `<`, `<=`, `>`, `>=` (6.16).
- Real specparams and specparams with a packed range, also as path delays and
  timing-check limits (6.20.5); a size cast of an unsigned operand folds in
  constant evaluation.
- `a[i++] += 2` and other updates with a side-effecting index evaluate the
  index once (11.4.1); `{a, v[i]} <= x` and `v[i] <= #(d) x` with a run-time
  index (10.4.2); a delayed nonblocking write outside the declared range has no
  effect (11.5.1).
- Bit and part selects written through a hierarchical reference
  (`u.s[1] = 1`); `g[0].D = 5` names a member of a generate block array
  element; a filename given as a packed value (`$fopen`, `$readmem*`); blank
  positional task actuals take their defaults.
- `@(posedge clk iff en)` on concurrent assertions (16.5): the guard was taken
  as the clock signal.
- `inner_t [1:0] e;` members of structures (7.4.1); `$cast` from a real
  expression rounds (6.12.2); `+:`/`-:` on an ascending constant select
  MSB-first; Verilog-2005 genvars are integers.
- Sequential UDPs: an input change that no table row covers sets the output
  to x (29.6).
- A pull port (`inout tri0 z`) merged with a plain wire actual makes the net
  that pull type (23.3.3.7).

VHDL:

- Component ports of every mode except linkage may have a default (6.5.2).
- An unlabeled concurrent procedure call with named association
  (`proc (a => 1, b => 2);`).
- Unconstrained STD.STANDARD array constants initialised by a positional
  aggregate take its length; a record signal of a package record type takes
  its elements' defaults (the package subtype was unlinked).
- STD.ENV TIME_TO_SECONDS and SECONDS_TO_TIME results take part in overload
  resolution.
- `F.e` for a parameterless function returning a record selects the element
  only when the record has it (8.3).

Runner: `wire real` and other real-valued built-in nets are an Icarus
extension (IEEE 1800-2017 6.7.1 allows only four-state integral net types).
34 ivtest cases use them; the 27 that fail are now out of scope (the 7 that
pass still count, as for other out-of-scope cases).

Measurement: the Batch 62 failures of the SystemVerilog suites were rerun on
frozen `b63a`, `b63b` and `b63c`, with every ivtest UDP, `$fopen` and
`$readmem` case and every Verilator and ivtest `tri0`/`tri1` case; the three
VHDL suites were run in full on `b63c`, since the femtosecond default changes
every VHDL design. A first full VHDL run on `b63a` showed four VESTs losses,
fixed before `b63c`: tc979, tc980 and tc990 (an invalid `F.e` element is again
rejected) and tc3188, which had passed only because TextIO READ ignored the
unit at 1 ns resolution. The 295-case fast set passed on `b63c`. Net against
Batch 62: **51 new passes, zero regressions**.

| Suite | Batch62 passes / in scope | Batch63 passes / in scope | Excluded |
|---|---:|---:|---:|
| ivtest | 2461 / 2657 | 2487 / 2630 | 196 |
| sv-tests | 1426 / 1497 | 1426 / 1497 | 0 |
| verilator | 1457 / 2216 | 1472 / 2218 | 197 |
| vests | 3545 / 3646 | 3547 / 3646 | 2 |
| nvc | 806 / 1262 | 813 / 1262 | 220 |
| vhdl-compliance | 37 / 72 | 38 / 72 | 0 |

ctest: the `vhdl`, `systemverilog` and `compliance` labels had drifted to 33
failures over Batches 60 to 63. Fixed here: two undocumented
`FSIM-ELAB-VHACCESS` codes (catalog now 2974 production codes), three files
without an SPDX notice, and unit tests that still expected behaviour the LRM
batches changed (`` `timescale `` inside a module is an error, split
`bufif`/`notif` drivers, null integer subtypes, array conversions that only
need equal lengths, run-time `**` exponents and `$countbits` controls,
anonymous-program callables in `$unit`, the 1 fs automatic resolution, a test
UDP without the `(??)` row that LRM 29.6 requires). 28 remain: 10 of the 11
known ones; the UVM audit, which counts an untracked top-level file in this
checkout; and 17 that fail the same way on the Batch 62 binary or could not be
tied to this batch: seven VHDL application tests rejected with
`FSIM-ELAB-VHSUBTYPE-003` (Synopsys and other IEEE package uses), three
compliance fixtures in both engines (`sv_class_handle_fixed_array_*`,
`vhdl_enum_index_arrays`),
`vhdl_vital_delays`, `artifact_phases`, `classes`, and a crash in
`native-frontier-a2-blocking-immediate`. They are left for a regression batch.

### Batch 62: runner flags, drivers, VHDL parsing gaps and full checkpoint

Five new triage lanes covered the compile- and elaboration-stage failures the
Batch 61 lanes had not (`.local-artifacts/lrm-closure/triage/b62-INDEX.md`).

Runner (no simulator change): Verilator driver flag lists are evaluated from
f-strings, `str()` and top-level literals; non-identifier `+define+` pieces are
dropped; `--timing` defines `VERILATOR_TIMING`; `+1364-2005ext+v` selects
Verilog-2005; ivtest root detection accepts attribute instances before
`module`. Eleven cases close on these alone.

SystemVerilog and Verilog:

- `0 ** -1` folds to x (11.4.3), and a `` `timescale `` inside a design element
  is an error (22.7). Edge-sensitive module paths take a plain `:` data source
  without an edge identifier (30.4.4).
- Nested zero replications inside a replication are ignored (11.4.12.1), and
  UDP table symbols need no separating white space (29.3.6).
- Disjoint continuous and procedural drivers of one variable are legal (6.5),
  as are drivers of disjoint VHDL subelements.
- Concatenation targets in functions; a sequence or property declared in a
  clocking block; genvars of a generate region have module scope; chandle
  equality with a function result; posedge/negedge of a vector expression
  takes its least significant bit (9.4.2).
- `rand_mode`/`constraint_mode` without parentheses and unqualified inside
  methods; object-level mode calls on class-handle properties; `Pkg::Cls h =
  new;` at module scope.
- `$cast(dst, src);` as a task reports an invalid cast at run time (8.16).

VHDL:

- Each driver of a STD_LOGIC signal starts at the signal's initial value, not
  'U' (14.7.5.2); records whose elements are all STD_LOGIC resolve.
- Conditional force assignments, force and release in procedures, labeled
  concurrent assignments to indexed targets, entity-level shared variables
  and disconnection specifications, `wait on` a record field or signal formal.
- A later primary unit of the same name in one source file replaces the
  earlier one (13.5); `use pkg."+"` parses.
- Named STRING subtypes as aggregate contexts; based-literal exponents
  (`16#E#E1`); STD.ENV simple names imported by `use std.env.all`, and the
  STRING overload of FILE_LINE; static subelement port actuals before 2008;
  bare-name calls of parameterless functions in constant folding.
- Array logical operators with operands of different lengths fail when they
  execute (9.2.2); type conversions between arrays of either direction keep
  only the length check; waveform ordering is checked statically only for
  literal delays.

Measurement: a full six-suite run on frozen `b62a` (11,940 IDs) against the
Batch 61 overlay showed 52 gains and 6 losses. All six losses were fixed and
re-verified on `b62b` (a SIGFPE in class-parameter division, a missing run-time
error for task-form `$cast`, user enumerations with lowercase character
literals, undefined `wait on` names, and a length mismatch previously caught
only by accident). Reverting the lowercase-character change reopened tc745,
tc746 and nvc vests3. The 295-case fast set passed on `b62b`. Net against Batch
61: **56 new passes, zero regressions**.

| Suite | Batch61 passes / in scope | Batch62 passes / in scope | Excluded |
|---|---:|---:|---:|
| ivtest | 2450 / 2657 | 2461 / 2657 | 169 |
| sv-tests | 1426 / 1497 | 1426 / 1497 | 0 |
| verilator | 1441 / 2216 | 1457 / 2216 | 199 |
| vests | 3536 / 3646 | 3545 / 3646 | 2 |
| nvc | 788 / 1262 | 806 / 1262 | 220 |
| vhdl-compliance | 35 / 72 | 37 / 72 | 0 |

This run is the full checkpoint for Batches 61 and 62; the next is due by
Batch 65.

### Batch 61: UVM identity, generate names, class calls and VHDL subtypes

Baseline: the Batch60 full six-suite checkpoint, re-run unchanged after the
duplication refactor (commit 9ad14244). Six read-only triage lanes ranked the
remaining failures by shared cause (`.local-artifacts/lrm-closure/triage/b61-INDEX.md`).

SystemVerilog and Verilog:

- UVM detection matched any name containing `uvm_`, so user classes named
  `uvm_object` or `uvm_test`, or declared in a package such as `ivl_uvm_pkg`,
  were taken over by the native UVM services. One shared predicate now accepts
  only identities in the UVM package (`support::is_uvm_library_identity`).
- `$cast(dst, src);` as a statement with a class destination is lowered, and a
  default `pre_randomize`/`post_randomize` called through `super` is empty
  (IEEE 1800-2017 18.6.2). `p.randomize` without parentheses is a call.
- Object-level `obj.rand_mode(b)` and `obj.constraint_mode(b)` set every random
  variable or constraint of the object (18.8, 18.9).
- Class-parameter defaults evaluate sized and based literals, casts, `?:`,
  shifts and logical operators.
- Generate regions: an unnamed `else` or case item holding only a nested
  generate construct adds no scope, and an `else` named like its `if` keeps
  that name (27.5). A generate block's own declaration no longer aliases a
  same-named object of an enclosing scope.
- Anonymous-program tasks and functions are compilation-unit callables.
- Gate-instance arrays accept bit-select and constant part-select terminals.
  A delay-free `bufif`/`notif` is two strength-limited drivers, so an unknown
  control drives L or H (28.6).
- A queue or dynamic-array formal accepts a concatenation actual (`{}`), and a
  container variable with a non-constant initializer gets a time-zero
  initializer process.
- Names inside a task or function called through an instance resolve from the
  callee's instance first (23.8). `$unit::name` names the compilation-unit
  scope. Function and task default arguments are resolved.
- A block-local variable was folded to its initializer when combined with a
  parameter (`a < N/2`); variables are no longer constant outside constant
  functions. `!x` has a 1-bit unsigned result.
- A sampled-value function without a clock argument in a concurrent assertion
  uses the assertion's clock (16.9.3). `p.status()` has its 32-bit width.
- Static task and function storage is not a multiple-driver conflict and adds
  no implicit sensitivity. A delayed continuous assignment with a run-time
  delay starts the net at X.
- `$isunbounded` in a constraint is false. `$ferror` reports only the most
  recent file operation, and `$ungetc(-1)` fails.
- An instance-array output port may connect to a constant part-select.

VHDL:

- `T'BASE'attr` (16.2.2) and user-defined attributes through an object alias.
- Null integer subtypes are legal; only objects of them are rejected.
- Integer `**` accepts a run-time exponent, checked nonnegative when it runs.
- Record and array element subtypes resolve in their declaring scope (an
  entity's subtype used by an architecture's record).
- Named constrained REAL and TIME subtypes may type a signal.
- Aliases of an element or slice can be assignment targets, aliases of
  constants evaluate, and `use inst.all` imports a local package instance.
- A deferred STRING constant can be indexed. A literal-only string
  concatenation takes a STRING target's type.
- Overloaded enumeration literals with the same position, or selected by the
  declared type of a local initializer, resolve. CHARACTER literals compare
  with a 32-bit loop parameter.
- An expanded name through the architecture (`arch.q`) names the signal, and a
  recursive architecture may bind its own instances while it is analyzed.

Libraries: analysing a configuration made every architecture of its entity
obsolete, because the catalog took the configured entity's name as the
configuration's primary-unit identity. A configuration is now its own primary
unit (IEEE 1076-2008 13.5).

Runner: the ivtest `fileline` case maps the bare `__FILE__` path, and chained
VESTs groups now honour `delete_lib`, so later chapters no longer re-analyse
over earlier chapters' same-named units.

Measurement on frozen `b61b`: every Batch60 failure was re-run (all six suites)
and the 295-case fast set passed 295/295. **71 new passes, zero observed
regressions.** Targeted reruns on `b61c` add pr544, pr1787394b,
switch_primitives, t_gen_for and sv-tests 18.8 `rand_mode_2`, and the
Ashenden chain on `b61d` adds ch_15 `dlxr`, `dlxstsv` and `dlxtstr` (79 in all,
fast set 295/295 on both). The table is an overlay on the Batch60 checkpoint.

| Suite | Batch60 passes / in scope | Batch61 passes / in scope | Excluded |
|---|---:|---:|---:|
| ivtest | 2436 / 2657 | 2450 / 2657 | 169 |
| sv-tests | 1424 / 1497 | 1426 / 1497 | 0 |
| verilator | 1412 / 2216 | 1441 / 2216 | 199 |
| vests | 3517 / 3646 | 3536 / 3646 | 2 |
| nvc | 773 / 1262 | 788 / 1262 | 220 |
| vhdl-compliance | 35 / 72 | 35 / 72 | 0 |

Several global changes (variable constant folding, sampled-value clocks,
logical-not signedness, delay-free tristate gates) are covered only by the fast
set and the failure re-run; the next full six-suite checkpoint is due in
Batch62 or Batch63.

### Batch 59: event lifetime, lexical types and VHDL composite shape

SystemVerilog fixes preserve declaration and instance identity for event arrays,
event aliases, forked automatic frames, synchronization ownership and output
argument copy-back. Scoped aliases and imports retain dimensions and signedness.
Instance-array port bridges check writable container counts, widths and nominal
types before connecting individual instances.

VHDL fixes retain expected types and lexical owners through concatenation,
STRING formals and results, nested array/record constraints and function-result
bounds. Case and selected-assignment checks use typed selector provenance and
complete coverage. Read-only INTEGER/TIME constant arrays support bounded
runtime selection. Materialized array dimensions retain their index bounds and
direction through type attributes; implicit STANDARD index types and CHARACTER
aggregate choices resolve with scope and identity guards. Use visibility,
block/component binding and selected architecture identity retain their nominal
context. Compilation also avoids repeated declaration-index refreshes and
callable-formal scans identified in the Ashenden workload.

The native overload fixture now supplies the complete eight-element slice to
its constrained formal. Its original three-element slice remains a negative
test. Traces proved that the rejected actual had the correct nominal type and
slice bounds but the wrong length; the product's length guard remains intact.

The `m59c` failure-union measurement covers 3,411 unique cases and 3,428 raw
occurrences across six suites. Combined with the fresh 295-case fast set, it
records **67 new passes, one scope exclusion and zero observed regressions**
against Batch58. All 67 gains pass on both interpreter and compiled engines:
30 VESTS, 26 NVC, ten Verilator and one VHDL-compliance case. The table is an
overlay: unmeasured cases retain their Batch58 status. The full six-suite
checkpoint remains due in Batch60.

| Suite | Batch58 passes / in scope | Batch59 passes / in scope | Total excluded |
|---|---:|---:|---:|
| ivtest | 2451 / 2657 | 2451 / 2657 | 169 |
| sv-tests | 1420 / 1497 | 1420 / 1497 | 0 |
| verilator | 1387 / 2216 | 1397 / 2216 | 199 |
| vests | 3481 / 3646 | 3511 / 3646 | 2 |
| nvc | 744 / 1263 | 770 / 1262 | 220 |
| vhdl-compliance | 31 / 72 | 32 / 72 | 0 |

NVC `integer2` expects wrapping for an expression beyond INTEGER's upper bound;
that expectation is a scope exclusion, not a passing gain. Legal INTEGER2019
bounds and invoked overflow rejection remain covered. No prior exclusions were
reopened. Verilator `t_recursive_method` changed from a frame-storage-budget
failure to a timeout and remains open; it was not a previously passing case.

Validation on frozen `wip59cd`: the build passed with no source-write events;
focused checks passed 121/121, and the broader labelled set passed 730/741 with
the same eleven known failures or dependency skips. Native overload checks,
STANDARD index controls, nominal-identity controls and bounded array controls
passed. The fast set passed 295/295. Retention covered 580 unique IDs and 597 raw
occurrences per engine, preserving all 66 required gains and 98 recovered
passes with no selected baseline-pass loss and matching engine statuses. The
additional measured gain, VESTS `tc1029`, was verified separately on both engines.
The live-root UVM source-package audit still reports the unrelated `.claude`
entry; no live-root UVM audit pass is claimed.

Remaining work includes local CHARACTER-shadow named-aggregate lowering,
automatic/event activation identity, typed-file nested STRING I/O, per-object
bounds and payload shape for access-to-unconstrained arrays and strings, and
recursive-method runtime behavior. Property-solver, class SimIR and UVM gaps
remain within the broader closure activity.

The executable SHA-256 is
`b1f6a5667222a3e0a869b4a360f5b8f5f20678929c5db476f55568828d213bb9`.
Local evidence is under `.local-artifacts/lrm-closure/work/batch59/` in
`m59c-comparison.json`, `final67-canonical-verification.json`,
`retention99-retention-assessment.json` and the associated pinned run reports.

### Batch 58: scoped types, formatted I/O and VHDL array values

SystemVerilog fixes preserve declaration and receiver identity for scoped types,
class handles, imports and generated callables. They cover array dimensions,
constant expression sizing, real-to-time assignment, and formatted scanning for
`%t`, `%u`, `%z` and end-of-file handling. HIR schema 11 retains source order,
block-import scope and ordered non-ANSI port slots; runtime schema 81 records
class-allocation owner identity.

VHDL fixes cover rank-two and unconstrained arrays, deferred constants,
attributes, Boolean and null array operations, generic aggregate context,
nominal type identity and TIME units. Direct-return function results can supply
proven rank-one array bounds to unconstrained procedure formals, including the
IEEE scalar/vector logical operators' `1 to N` result bounds. This inference
requires one direct return, no additional local declarations and a statically
proven same-nominal array shape. One-element arrays remain distinct from scalar
overloads. Scalar logical chains reject inapplicable vector analysis before
recursive shape queries; the original Ashenden overflow case passes within
the full ordered prerequisite chain.

The VUnit setup shim now models five notification handshakes with ten delta
waits. Replaying that shim on frozen Batch57 still produces an incorrect value
for `tb_array_scalar`, separating the setup correction from the compiler's
function-result repair. Original corpus sources remain unchanged.

The `m58c` failure-union measurement covers 3,411 unique cases across six suites.
Combined with the 295-case fast set, it records **61 new passes, five scope
exclusions and zero observed regressions** against Batch57. All 61 gains pass
on both interpreter and compiled engines. The table is an overlay: unmeasured
cases retain their Batch57 status. The next full six-suite checkpoint is due
in Batch60.

| Suite | Batch57 passes / in scope | Batch58 passes / in scope | Total excluded |
|---|---:|---:|---:|
| ivtest | 2427 / 2660 | 2451 / 2657 | 169 |
| sv-tests | 1420 / 1497 | 1420 / 1497 | 0 |
| verilator | 1386 / 2216 | 1387 / 2216 | 199 |
| vests | 3458 / 3647 | 3481 / 3646 | 2 |
| nvc | 732 / 1264 | 744 / 1263 | 219 |
| vhdl-compliance | 30 / 72 | 31 / 72 | 0 |

The five new exclusions are separate from passing gains. NVC `attr16` and
VEST `simple-range-attribute` apply scalar bound attributes to `A'RANGE`;
ivtest `br_ml20150315b` expects rejection of a legal unpacked struct,
`br_gh840b` expects an optional out-of-range-select diagnostic, and `scanf4`
expects zero instead of EOF when whitespace-only input ends before the first
conversion.

Validation on frozen `wip58ae`: the full build passed, focused checks passed
145/145, the labelled suite passed 668/679 with exactly the 11 known failures,
and the fast set passed 295/295. Source-package validation passed for 2,839
files; runner unit checks passed 30/30. Archive, source, input and exact case
inventory checks passed. STRING concatenation context and nested record-field
subtype gaps remain open for Batch59.

The executable SHA-256 is
`b955fc43ecd0ce246ca398d0c361efad947b615e8a46711c4d0c62b81ce94bbb`.
Local evidence is in
`.local-artifacts/lrm-closure/work/batch58/ae-publication-proof.json` and
`targeted-gains-final-ae.json`, with unique-case overlays under
`.local-artifacts/lrm-closure/overlays/b58-<suite>/`.

### Batch 57: array type context, interface types and TextIO

- VHDL aggregates retain the expected type's declaration scope while resolving
  their operands in the use scope. Qualified and nested array-valued segments
  preserve nominal type identity, element widths, cardinality and index bounds.
  Local types that shadow predefined or package names remain distinct.
- Unconstrained array formals recover bounds from matching actual subtypes and
  supported function results. INTEGER_VECTOR qualifications and formals use
  scoped predefined-type checks. CHARACTER arrays retain their element width;
  constrained STRING values preserve NUL bytes and enforce length constraints.
- VHDL component bindings retain composite generic defaults, unconstrained array
  port shapes and operator actuals. STRING port adapters preserve the value and
  bounds separately. Access-object initialization rejects a designated record
  value without an allocator while accepting allocation, null and access copies.
- TextIO handles dynamic field widths, hexadecimal and octal codecs, and
  dynamically constrained STRING reads. Focused fixtures cover successful reads,
  failure flags, LINE consumption and unchanged targets after failed radix reads.
- SystemVerilog interface type actuals preserve packed dimensions and declaration
  context across specialization and nested ports. Array queries, streaming slice
  types, constant shift counts and preprocessor source locations have focused
  regression coverage. Mutable conditions are not treated as constant guards.
- Runner fixes preserve source-location expectations without changing corpus
  files, maintain ordered VEST prerequisites, and recognize the expected runtime
  range failure in nvc issue502. Two __FILE__ cases are runner corrections and
  must be reported separately from compiler/runtime gains.

Scope exclusions are separate from passes: ivtest pr1787423, pr1787423b and
pr2834340 use multiple output terminals in one pullup/pulldown instance;
sv_typedef_nested_array expects Icarus dimension ordering; nvc agg7 expects a
descending array-valued association in an ascending constrained target.

Validation on frozen wip57af: Build53 passed; all 109 focused checks and all 33
analysis-error checks passed. The labelled suite passed 574/585, with exactly
the 11 recorded baseline nonpasses. The fast set passed 294/295 with no
regressions; tb_array_scalar remains a known failure. Source-package validation
passed for 2791 files. The live tracked patch and all 40 registered new files
matched the frozen source evidence.

The full six-suite checkpoint (`m57final-af`) records **77 new passes and five
scope exclusions, with zero regressions** against the corrected Batch56
baseline. The passes comprise 75 compiler/runtime gains and two runner
corrections. All 77 passed on both interpreter and compiled engines. The
original tc2552 negative case also passed on both engines with the expected
VHASSIGN-001 diagnostic. Archive, input, corpus revision and exact inventory
checks passed before and after qualification.

| Suite | Corrected Batch56 passes / in scope | Batch57 passes / in scope | Excluded |
|---|---:|---:|---:|
| ivtest | 2416 / 2664 | 2427 / 2660 | 166 |
| sv-tests | 1418 / 1497 | 1420 / 1497 | 0 |
| verilator | 1369 / 2216 | 1386 / 2216 | 199 |
| vests | 3434 / 3647 | 3458 / 3647 | 1 |
| nvc | 712 / 1265 | 732 / 1264 | 218 |
| vhdl-compliance | 27 / 72 | 30 / 72 | 0 |

The comparison uses a corrected Batch56 baseline. Exact checks on frozen
wip56u disproved 29 inherited overlay passes: four Icarus, four Verilator,
six nvc and fifteen VEST cases. The original records remain unchanged; a
hash-backed derived baseline records those corrections. This is a bounded
baseline repair, not a new full Batch56 run. The final Batch57 checkpoint is
a full six-suite interpreter run, with 3648 unique VEST cases and 3665 records
because the ordered prerequisite chain repeats 17 cases.

The final executable SHA-256 is
`876bfbec00ffbede40a62bbee2a0bc5529a54e9cbc5aa162063b4e79f3a3d81b`.
The local final ledger is
`.local-artifacts/lrm-closure/work/batch57/targeted-gains-final-af.json`;
its result references retain both-engine verification and the full checkpoint.

### Batch 56: array values, casts, VHDL aggregates and bindings

- SystemVerilog dynamic arrays and queues copy values, including empty sources;
  queue method results retain their element type. Fixed-array equality accepts
  equivalent element types and extents with different bounds (1800-2017 6.22.2,
  11.2.2). Loop declarations retain the lifetime required by 12.7.1.
- Integral sequence bit-stream casts preserve element order and validate sizes
  (6.24.3). Typedef casts resolve scalar real and string aliases. Parenthesized
  constant expressions can supply a size cast; runtime widths and nonintegral
  operands are rejected. Task copy-out conversions use formal signedness.
  Conditional expressions retain their intrinsic width in narrower contexts.
- Verilog multidimensional arrays retain each declared dimension (1364 4.9).
  Escaped macro names keep a consistent identity across preprocessing and cache
  use; continued comments preserve directive boundaries. Packed-string sscanf
  sources are evaluated once before outputs are written.
- VHDL aggregate assignment targets and bounded array elements share shape
  inference with qualified aggregates and comparisons. CHARACTER index subtypes
  and aliases preserve ordinal bounds while user enumeration types retain their
  own positions. Access values support implicit dereference and formal aliases.
- VHDL constant equality preserves packed array lengths. Unconstrained function
  and access subtypes do not supply a proven width for mismatch folding.
  Guarded resolver variants follow the same interpreter route as their base
  program, preserving the driving-value input when surrounding processes use
  compiled execution.
- VHDL component configurations retain selected names, nested scope and default
  bindings. Binding maps evaluate locally static actuals with the component
  generic environment; character and packed bit-vector actuals keep their type.
- TextIO READ overloads, VHDL-2008 SREAD, SIDE and JUSTIFY follow their package
  visibility and revision rules. Procedure-local files close with their scope.
  Typed CHARACTER-array files round-trip through fsim's own writer. STRING
  aggregate locals preserve NUL and space bytes; constrained STRING signals
  support character indexing.
- Runner: additional VEST typed-file readers retain their own writer as a
  prerequisite. Comment and escaped-identifier scanning preserves standard
  source interpretation. The VHDL HIR schema is now 7; stale artifacts are
  rejected with the current schema diagnostic.

Excluded this batch: ivtest `sv_cast_darray` casts REAL array elements to packed
bits, outside the integral bit-stream rules; `macro_with_args` depends on
Icarus-specific stringification. Neither corpus source was modified.

Investigated and left open: nested-interface type mismatches after size-cast
lowering, broader composite generic defaults and port compatibility, STRING
constant indexing and default initialization, TextIO hexadecimal/octal codecs,
ordered packed constant comparisons, comparison of two dynamically shaped
unconstrained function results, and mixed-width array aggregate targets.

Validation: full release build at Build32, followed by focused rebuilds through
Build40; all 84 focused checks pass, including the 36 new positive fixtures in
both engines and the restored guarded resolver and range constraints witnesses.
The VHDL-2008-only SREAD analysis fixture rejects earlier revisions. Labelled
CTest passes 495/506 with exactly the 11 previously recorded failures. Diagnostic
catalog: 2959; corpus-runner unit tests: 24/24. Source-package inventory: 2750 files.

Qualification uses frozen `wip56u`: the standing failure union (`m56b`),
fast set (`m56bfs`), and the matching full Ashenden prerequisite supplement.
The Ashenden run contains 463 records / 446 unique cases, with exactly five
new passes and no regressions; only those five cases supplement the overlay.
The unique-case result is **69 new passes and two scope exclusions**, with
no observed regressions against the corrected baseline.

| Suite | Batch55 passes / in scope | Batch56 passes / in scope | Excluded |
|---|---:|---:|---:|
| VESTs | 3418 / 3647 | 3449 / 3647 | 1 |
| nvc | 704 / 1265 | 718 / 1265 | 217 |
| VHDL-Compliance | 26 / 72 | 27 / 72 | 0 |
| ivtest | 2403 / 2666 | 2420 / 2664 | 162 |
| Verilator | 1367 / 2215 | 1373 / 2216 | 199 |
| sv-tests | 1418 / 1497 | 1418 / 1497 | 0 |

Qualification exposed a historical VUnit runner classification error: seven
Batch55 parent records were marked passed despite failed recorded checks.
The corrected existing overlay has 26/72 VHDL-Compliance passes rather than
33/72. The original records remain preserved with a hash-backed derived view.
The runner now propagates child failures, checks completion and cleanup counts,
and retains failures outside named tests. All 72 VHDL-Compliance cases were
rerun on the same frozen candidate with this correction: 27 pass, 45 fail,
with 89/154 recorded child checks passing and no newly failing parent cases.
These results replace the earlier VHDL-Compliance subset in the final overlay.

The fast set has 291/295 actual passes; its four remaining VUnit failures
(`tb_aggregate`, `tb_array_scalar`, and matching equality/inequality) were
already recorded as failed checks in Batch55. They are retained as open gaps,
not accepted as passes. Verilator `t_func_return_init` now passes and returns
to the in-scope denominator. These are failure-overlay counts, not a full
six-suite qualification; the next full checkpoint is Batch57.

### Batch 55: static callable storage, VHDL binding, indexing and typed files

Fixtures: `sv_static_callable_storage.sv`, `sv_static_interface_storage.sv`,
`sv_bounded_queue_function_conversion.sv`,
`sv_constant_function_and_wide_literals.sv`,
`vhdl_lexical_replacement_characters.vhd`,
`vhdl_component_binding_lrm.vhd`, `vhdl_guarded_bus_port.vhd`,
`vhdl_user_attribute_subtype_context_20261013.vhd`,
`vhdl_chained_function_result_indexing.vhd`,
`vhdl_function_result_array_of_arrays_indexing_20261013.vhd`,
`vhdl_partial_function_result_indexing_20261013.vhd`,
`vhdl_typed_file_long_row_roundtrip_20261013.vhd`,
`vhdl_typed_file_string_write_roundtrip_20261013.vhd`,
`vhdl_record_member_preservation_file_roundtrip_20261013.vhd`,
`vhdl_record_array_member_dynamic_index_20261013.vhd`.

- Static lifetime: task/function locals, formals, implicit results and named
  blocks use instance-owned storage, retain values across calls, and expose
  hierarchical names (IEEE 1800-2017 6.21, 13.3; IEEE 1364 task/function
  lifetime). Packed values, strings and containers share the same lifetime
  rule; automatic variables remain per activation. Interface calls use the
  selected instance's storage. Static formals copy in/out of shared storage;
  overlapping calls therefore observe that shared state.
- Queue assignment and function returns convert between queue bounds,
  retaining only the elements that fit a bounded destination (1800 7.10).
  Constant-function locals receive their type's default value; wide unsized
  based literals retain their full width (5.7.1, 6.8, 13.4.3). Negative
  localparams keep their parameter identity through normalization, fixing
  the long-standing `param-extend` regression in unsigned expressions.
- VHDL port associations: `inertial` expression actuals receive expression
  adapters (1076-2008 6.5.6.3); slice-by-slice output associations copy only
  the corresponding formal slice. Aggregate assignments to slices use the
  slice's index range. Selected protected-object names visible through use
  clauses resolve in the object scope.
- VHDL physical time: decimal real literals scale exactly to femtoseconds
  before rounding. Function, procedure, generate and configuration context
  items retain their use clauses.
- VHDL lexical and configuration forms: replacement `!`/`%` characters,
  guarded bus ports, omitted/default bindings and selected component names
  parse with their standard meanings. An entity with only defaulted
  generics can bind an empty generic component interface; empty `all`
  configuration specifications are legal. An omitted architecture selects
  the most recently analyzed architecture when source order is available.
- VHDL user attributes retain their declared subtype for aggregate and
  selected-value contexts. Each parenthesized index group supplies all
  dimensions of its array subtype (1076-1993 6.1); chained groups descend
  into array-valued elements. Function results are evaluated once before
  selection. Partial multidimensional groups are rejected.
- VHDL typed files: STRINGWRITE's TEXT shortcut applies only to TEXT;
  fixed-shape files preserve record boundaries, long rows and CHARACTER
  bytes. VHDL reads are no longer subject to the SystemVerilog 4096-byte
  line limit. STRING record-member writes preserve adjacent fields.
  The execution-context adapter retains the public object layout and vtable
  and uses the existing serialized/JIT-hashed FileOpen VHDL flag.
- Runner: VEST typed-file readers execute after their own writers in one
  workspace, without importing another simulator's file representation.
  Filtered runs retain both members and remove stale files before writing.
  Five runner unit tests cover the chaining behavior.

Excluded this batch: Verilator `t_static_task_args` expects independent
recursive input arguments in a static task (1800 13.3.2), and VESTs `tc300`
expects an implementation-dependent REAL overflow to be an analysis error
(1076-1993 3.1.4/3.1.4.1). Typed-file readers remain in scope.

Investigated and left open: runtime/time-zero static initializers, unnamed
process-block locals, package callable storage coverage, automatic local
events and hierarchical block defparams; defaulted function calls whose
parentheses need late disambiguation from array indexes; configuration
maps that refer to component generics; broader VHDL user-attribute and
runtime-bound array cases. This batch does not claim complete static
storage or language closure.

Measurement: the union of the b51a and b54b failure sets in all six suites
(`m55a`, frozen `wip55-luna6`), followed by the 295-case fast set
(`m55afs`), overlaid on b54b. There are **66 closures: 64 new passes and
two exclusions**, with no observed regressions. New passes: ivtest 9,
Verilator 4, VESTs 35 and nvc 16. This is a failure-set overlay, not a new
full-suite qualification; the next full six-suite run is due at Batch 57.
The fast set passes all 295 cases.

Validation: full release build; labelled CTest 422/433, with exactly the
11 previously recorded failures; diagnostic catalog 2957; runner unit
tests 5/5; source-package manifest 2713 files in a clean tracked-source
snapshot including registered new files. Focused fixtures pass in both
engines, including the NUL/space typed-record round trip and dynamic
record-member indexes. A direct file-runtime test confirms preservation
of the SystemVerilog 4096-byte limit; its later missing-file exception
expectation fails identically on the pre-file-fix control and remains
open. The aggregate runtime test stops at an earlier known failure.

Excluded (out-of-scope) cases:

- ivtest 160: Icarus diagnostics in gold output (53), Icarus system tasks
  (23), Icarus process order (22), implementation-defined or Icarus-only
  results (22), iverilog options and test types (18), Icarus syntax (11),
  SystemVerilog features in Verilog-2005 mode (11).
- Verilator 200: SystemC, PLI/VPI or DPI harness (85), `$c` (32),
  non-standard behaviour, flags and configuration (30), C++ harness (24),
  lint-only (21), missing top file (8).
- nvc 217: nvc-specific test kinds (208), nvc options (5), named work
  libraries (2), nvc operator extensions (2).
- VESTs 1: implementation-dependent REAL overflow detection (`tc300`).
- sv-tests and VHDL Compliance-Tests: none.

| Suite | Batch 54 (wip54e) | Batch 55 (wip55-luna6 overlay) |
|---|---:|---:|
| VESTs | 3383/3648 | 3418/3647 |
| nvc regress | 688/1265 | 704/1265 |
| VHDL Compliance-Tests | 33/72 | 33/72 |
| ivtest | 2394/2666 | 2403/2666 |
| Verilator test_regress | 1363/2216 | 1367/2215 |
| sv-tests | 1418/1497 | 1418/1497 |

### Batch 54: constant-expression widths, hierarchical typedefs and members, unpacked slices and concatenations, nvc Verilog

Fixtures: `sv_language_details_batch54.sv`, `vhdl_assertion_sensitivity.vhd`.

- Constant expressions: operands take the width of the whole expression
  before their operators apply, so carries are kept (`3'd4 * 3'd4 * 2 + 3'd3`
  is 35), through unary `-`/`~`, `?:` and relational operands (11.6.1,
  11.8.2). A parameter without a type has the width of its value (6.20.2).
  Division or modulus by zero gives x, and a real division gives an IEEE 754
  infinity or NaN (11.4.2). The real math functions are constant functions
  (11.2.1, 20.8.2).
- Generate: an unknown condition selects the else branch (12.4, 27.5).
  `blk.P` names a parameter of a selected generate block (27.6).
- Hierarchical names: `$bits(T)` of a type parameter and `$bits(inst.t)` of
  another instance's typedef (20.6.2); members of a packed structure signal
  read and sized hierarchically (`bus.rs.data`); a nested interface instance
  reached through an interface port (`io.types.P`).
- Unpacked arrays: continuous and nonblocking slice copies (the nonblocking
  form publishes each element in the NBA region); continuous and nonblocking
  assignment patterns; unpacked concatenations assigned to a fixed array or
  a slice (10.10); a conditional whose alternative is a pattern or function
  (11.4.11); `new[n](q)` with a queue initializer (7.5.1, 7.6).
- Ports and selects: an output port connected to a select partly or wholly
  outside the declared range, and a continuous assignment to such a select,
  write only the in-range bits (11.5.1).
- Processes: `@*` with no readable signals never runs (9.4.2.2); `a[(i)] <=
  #1 v` parses as an intra-assignment delay.
- Classes: members of unpacked-structure class properties; a property
  declared with a class type parameter has the specialization's type.
- VHDL: a concurrent assertion is sensitive to the longest static prefix of
  each signal name in its condition (11.5, 10.2); a null waveform element
  to an unguarded signal is an analysis error (10.5.2.2); an external name
  must have the type of the signal it denotes (8.7).
- Runner: nvc's Verilog tests compile as SystemVerilog, like the Verilator
  suite (they use `++` and `+=` in .v files).
- Out of scope:
  - ivtest: SystemVerilog preprocessor features in Icarus's default
    Verilog-2005 mode (`` `" ``, `` `__LINE__ ``; 8 cases), ``` ``NAME ```
    stringification (5), an omitted positional parameter override (1),
    `$urandom_range` in Verilog-2005 mode (3), scalar `new[]` initializers
    (2), and `?:` of two z values under an unknown condition expected to
    give z (1; Table 11-20 gives x).
  - Verilator: `t_math_pow3`, which keeps a power operand signed in an
    unsigned comparison (11.8.1).
  - nvc: `binary8` and `wide7`, which use `~&` and `~|` as binary operators.

Regressions found by the full run and fixed: four MOS-switch ivtest cases
(batch 53's `?:` z/z change; switches now pass z data through as z),
`enum_method_signed1`/`3` (batch 52 folded enumeration methods without
their width and sign; they are now folded only inside constant functions),
VESTs `tc1329` and nvc `issue907` (batch 53; see above). One remains:
ivtest `param-extend`, which fails since batch 51: an untyped `-1`
parameter is sign-extended in an unsigned expression.

Investigated and left open: `$bits` of interface members as parameter
actuals; class functions returning unpacked arrays; static function locals
kept between calls; unsized literals wider than 32 bits; string literals as
packed values in untyped parameters; the parameterized-class runtime for
`Bar#(Baz)` properties; VHDL binding generic maps that reference component
generics.

Measurement: a full six-suite run (b54b) on wip54e and the fast set,
compared with batch 53. 51 cases close (ivtest 6, Verilator 20, nvc 25);
the only case that passed in batch 53 and fails now is `param-extend`
(above), and the fast set passes. The first full run (b54a, wip54d) found
the regressions listed above, which batch 54 fixes.

ctest: the `vhdl`, `systemverilog` and `compliance` labels fail only the 11
known tests.

Excluded (out-of-scope) cases:

- ivtest 160: Icarus diagnostics in gold output (53), Icarus system tasks
  (23), Icarus process order (22), implementation-defined or Icarus-only
  results (22), iverilog options and test types (18), Icarus syntax (11),
  SystemVerilog features in Verilog-2005 mode (11).
- Verilator 199: SystemC, PLI/VPI or DPI harness (85), `$c` (32),
  non-standard behaviour, flags and configuration (29), C++ harness (24),
  lint-only (21), missing top file (8).
- nvc 217: nvc-specific test kinds (shell, VHPI, cover, wave, mixed, Tcl,
  relaxed, plugin; 208), nvc options (5), named work libraries (2), nvc
  operator extensions (2).
- sv-tests, VESTs, VHDL Compliance-Tests: none.

| Suite | Batch 53 (wip53c) | Batch 54 (wip54e) |
|---|---:|---:|
| VESTs | 3383/3648 | 3383/3648 |
| nvc regress | 663/1267 | 688/1265 |
| VHDL Compliance-Tests | 33/72 | 33/72 |
| ivtest | 2389/2685 | 2394/2666 |
| Verilator test_regress | 1343/2217 | 1363/2216 |
| sv-tests | 1418/1497 | 1418/1497 |

Batch 53's ivtest, Verilator and nvc columns use batch 53's scope; batch 54
excludes 19 more ivtest cases, 1 more Verilator case and 2 more nvc cases.

### Batch 53: unpacked-array parameters, class property members, guarded signals, VHDL runtime checks

Fixtures: `sv_language_details_batch53.sv`, `vhdl_guarded_and_real_ranges.vhd`,
and the analysis-error fixtures `unguarded_guarded_target.vhd`,
`deferred_constant_early.vhd` and `string_index_range.vhd`.

- Parameters: unpacked-array parameters are specialized per instance,
  so an instance can override them with an array actual
  (`#(.P('{...}))`, positional, nested or `default:` patterns, or a
  parent's array parameter) (6.20.2, 23.10). Unpacked arrays of
  structures take positional, member-keyed and `default:` patterns
  (10.9.2). Constants of specialized classes (`C#(7)::v`) and class
  parameters appear in module parameter defaults and overrides (8.25).
- Classes: a member of a structure-typed property (`c.p.a`) is read and
  written through the handle, inside and outside methods (8.4). Assigning
  a non-enumeration value to an enumeration property is an error
  (6.19.3).
- Statements: a nonblocking assignment to a concatenation of variables
  (10.4.2); string `compare` and `icompare` give signed results (6.16.8);
  `?:` with an unknown condition merges two `z` operands to `x` (11.4.11).
- Lexical: a time literal's unit must follow the number with no space,
  except when both come from one macro (5.8); modport expressions may be
  dotted member names (25.5.4).
- VHDL guarded signals: `bus` and `register` signals with guarded
  assignments in blocks, null waveform elements and `disconnect`
  delays. A register signal whose drivers are all disconnected keeps
  its value (6.4.2.3, 11.6, 7.4). An unguarded concurrent assignment to a guarded signal, or an
  aggregate target mixing guarded and unguarded signals, is an error
  (11.6, new `FSIM-VHDL-SEM-129`).
- VHDL runtime checks: a REAL value assigned outside its subtype's range
  (including infinities and NaN), a negative `wait for` timeout, and a
  STRING index outside its range stop the simulation with a failure.
  A static STRING index outside a constrained range is an analysis
  error (5.2.5, 10.2, 8.4).
- VHDL analysis: a package body naming a deferred constant before its
  full declaration is an error (4.8, new `FSIM-VHDL-SEM-130`); a function
  that ends without `return` fails at run time instead of at analysis.
- VHDL elaboration: implicit dereference of access values by index or
  member (8.3); aliases of external names (8.7); block port maps with
  conversion functions; `'length`/`'left`/`'right` of subprogram formals
  as constants; unbound generics use their defaults in constant
  initializers; enumeration-indexed bit-array constants; local vector
  constants up to 65536 bits.
- Runner: 41 more ivtest cases are out of scope because their expected
  output depends on Icarus's own choices. These are:
  - process order within one time step (22; IEEE 1800-2017 4.7 and
    1364-2005 9.9 leave it undefined);
  - implementation-defined formatting, widths and conversions (12);
  - `__ICARUS__` or Icarus options (3);
  - Icarus's VCD layout, an ignored SDF file, and `z` read from undriven
    variable bits (4).

  17 more Verilator cases are out of scope: Verilator command-line flags
  or configuration files (6), two-state initial values (5), a SystemC
  harness (1), and five that depend on the Verilator driver or report
  Verilator-specific results. Warnings in ivtest gold output count as
  Icarus diagnostics (10 more cases), and gold comparisons use outputs up
  to 16 MB.

Investigated and left open: nested block GUARD signals (tc3155, tc3156);
resolution of a bus signal with no connected drivers (the resolution
function's null array); record element range constraints given inline
(tc489); upward hierarchical references needing global deferral;
external names with relative paths or written to; deferred constants
folded into slice bounds lose their type (tc1133); real to integer
conversion wider than 64 bits; `%E`/`%G`; arrays of events.

Measurement: the batch 51 run b51a with its failures and the fast set
rerun on wip53c. 53 cases close (ivtest 4, sv-tests 2, Verilator 8,
VESTs 31, nvc 8); no case that passed in batch 52 fails, and the fast
set passes.

ctest: the `vhdl`, `systemverilog` and `compliance` labels fail only the 11
known tests.

Excluded (out-of-scope) cases:

- ivtest 141: Icarus diagnostics in gold output (54), Icarus system tasks
  (23), Icarus process order (22), implementation-defined or Icarus-only
  results (19), iverilog options and test types (18), Icarus syntax (5).
- Verilator 198: SystemC, PLI/VPI or DPI harness (85), `$c` (32),
  non-standard behaviour, flags and configuration (28), C++ harness (24),
  lint-only (21), missing top file (8).
- nvc 215: nvc-specific test kinds (shell, VHPI, cover, wave, mixed, Tcl,
  relaxed, plugin; 209), nvc options (4), named work libraries (2).
- sv-tests, VESTs, VHDL Compliance-Tests: none.

| Suite | Batch 52 (wip52e) | Batch 53 (wip53c) |
|---|---:|---:|
| VESTs | 3352/3648 | 3383/3648 |
| nvc regress | 655/1267 | 663/1267 |
| VHDL Compliance-Tests | 33/72 | 33/72 |
| ivtest | 2385/2736 | 2389/2685 |
| Verilator test_regress | 1335/2234 | 1343/2217 |
| sv-tests | 1416/1497 | 1418/1497 |

Batch 52's ivtest and Verilator columns use batch 52's scope; batch 53
excludes 51 more ivtest cases and 17 more Verilator cases.

### Batch 52: virtual-interface methods, string casts and selectors, constant string functions, VHDL physical units

Fixture: `sv_language_details_batch52.sv` (passes on xsim except two
constant-function checks: xsim rejects `e.next` in a constant function and
evaluates a string built in a constant-function loop wrongly).

- Virtual interfaces: a task or function called through a handle runs on
  the instance the handle selects at run time; bit-, part- and dynamic
  bit-selects of a member are written through the handle (25.9).
- Modport expressions may name a signal of a nested interface instance
  (`.r(inner.val)`), and a port using such a modport can be passed on to a
  lower module; a signal exposed both as an input and as an output of one
  modport stays writable through the output (25.5.4).
- Strings: a `case` with a string selector (12.5); `string'(x)` and
  vector casts of strings (6.16, 6.24.1); methods without parentheses
  (`s.len`, `s.toupper`, 13.5.5); a static class function without
  parentheses (`C::f`, 8.10).
- Constant functions may return strings and use enumeration methods,
  chained and without parentheses (`e.next.next`, `e.name`), for enums
  declared in the module, a package or `$unit` (13.4.3, 6.19.5); a module
  string variable may be initialized by such a function call.
- Classes: `super.new` with call arguments (8.17); `obj.srandom(seed)` and
  `this.srandom` (18.13.3).
- Containers: `void'(q.pop_front())` (7.10.2), `c[i]++` inside an
  expression, `name` of an enumerated member of a structure or array
  element.
- Continuous assignment to a member of an unpacked structure, and net
  element patterns (10.3, 10.9).
- Parameters: members of structure-typed parameters in constant
  expressions (`P.a`), keyed patterns as structure parameter defaults, and
  positional patterns of packed arrays with real items (6.20.2, 10.9).
- Block-local typedefs used as declaration types (6.18); `$dumpfile` of a
  packed filename; a fork's block-item declaration in a loop keeps one
  debug local.
- VHDL: nested subprograms read and write the locals of the enclosing
  subprogram (1076-2008 4.3); a bare physical unit name is a value of its
  type (5.2.4); `&` with a string literal and a bit or `std_ulogic`
  vector (9.2.5); index constraints and `'left`/`'right` of subtypes named
  by character or enumeration subtypes; positional aggregates of `bit`
  as signal initializers.
- Runner: Icarus `$deposit` and `$finish_and_return` are Icarus
  extensions; Verilator `t_virtual_interface_method` (toggles a clock that
  starts at x), `t_wait_iface_vif` (an `always @*` expected to run without
  an event) and five cases that call `$sformatf` in a constant function or
  expression (`t_const_string_func`, `t_display_string`,
  `t_display_string_noopt`, `t_display_enum_format`,
  `t_unroll_nested_param`; 13.4.3 allows only the constant system
  functions of 11.2.1) are out of scope.

Investigated and left open: overriding unpacked-array parameters
(`#(.P('{...}))`; they are kept as constant containers, about 8 Verilator
cases); arrays of named events and event handles in classes; modport
expressions with part-selects or interface-array elements; VHDL signal
formals assigned with a run-time `after` delay; streaming of unpacked
operands; class-parameter constants in module constants.

Measurement: the batch 51 run b51a with its failures and the fast set
rerun on wip52e. 55 cases close (ivtest 8, Verilator 24, VESTs 15, nvc 8);
no case that passed in batch 51 fails, and the fast set passes.

ctest: the `vhdl`, `systemverilog` and `compliance` labels fail only the 11
known tests.

Excluded (out-of-scope) cases:

- ivtest 90: Icarus diagnostics in gold output (44), Icarus system tasks
  (23), Icarus syntax (5), iverilog options and test types (18).
- Verilator 181: SystemC, PLI/VPI or DPI harness (84), `$c` (32), C++
  harness (24), lint-only (21), missing top file (8), non-standard
  behaviour (12).
- nvc 215: nvc-specific test kinds (shell, VHPI, cover, wave, mixed, Tcl,
  relaxed, plugin; 209), nvc options (4), named work libraries (2).
- sv-tests, VESTs, VHDL Compliance-Tests: none.

| Suite | Batch 51 (wip51f) | Batch 52 (wip52e) |
|---|---:|---:|
| VESTs | 3337/3648 | 3352/3648 |
| nvc regress | 647/1267 | 655/1267 |
| VHDL Compliance-Tests | 33/72 | 33/72 |
| ivtest | 2377/2740 | 2385/2736 |
| Verilator test_regress | 1311/2241 | 1335/2234 |
| sv-tests | 1416/1497 | 1416/1497 |

Batch 51's ivtest and Verilator columns use batch 51's scope; batch 52
marks 4 more ivtest and 7 more Verilator cases out of scope.

### Batch 51: packed structure arrays, packed array patterns, `%t` units, genvar shadowing

Fixture: `sv_language_details_batch51.sv`.

- Packed arrays of packed structures: `s_t [1:0] p` in declarations, ANSI
  and non-ANSI ports and typedefs; members of elements through constant and
  runtime indices (`p[i].hi`); multidimensional packed members
  (`s.y[1][3:0]`) (7.4.1, 7.2.1).
- Assignment patterns for packed arrays: positional, keyed, `default`,
  replicated and nested items, including members of packed structures and
  elements of unpacked arrays (10.9.1).
- `%t` converts an integral argument from its module's time unit, `$time`
  is formatted as a value, and `$sformatf` `%t` takes its argument (21.2.1.3,
  20.3); the static behavioral kernel runs `$time`, `$stime` and
  `$realtime`.
- A genvar hides a module variable of its name inside the loop-generate
  block's processes (27.4).
- Hierarchical calls: a module-instance or interface function called as a
  statement (23.8, 13.4.1); an output port connected to an element of a
  multidimensional packed array (`.out(q[0][g])`).
- `foreach (a[])` with every loop variable omitted runs no iteration
  (12.7.3); `disable` of the block of a sibling fork branch ends that branch
  (9.6.2).
- An output net port's initializer is a continuous assignment (23.2.2.3);
  a class handle converts to the interface classes it implements (8.26);
  an element of an unpacked array parameter folds (6.20.1); a size cast in
  an unsigned constant expression extends as unsigned (11.8.2); a real
  hierarchical variable as a delay (10.3.3).
- `$random` without a seed uses one seed starting at zero, as in
  `$random(seed)` (20.15); `$feof` of an invalid descriptor is true and
  `$ungetc` on it fails (21.3); `cmos` conducts when either half is on
  (28.9).
- Parsing: `timeunit` followed by `timeprecision`, `$` for integral
  parameters, time literals rounded to the precision, `` `\`" `` in macro
  strings, empty `.p()` parameter overrides, `buf #d (...)`, `%u`/`%z` in
  displays, `wait_order` with only `else`, `interconnect`, compilation-unit
  subroutines only in SystemVerilog, unpacked structure member defaults,
  `++` of a class property in an expression.
- The batch 50 change to `%d` of a real at run time printed a bare
  `$display(r)` as an integer; `%d` of a real now converts in the format
  lowering only.
- Runner: Verilator `--top-module`/`--top` select the elaborated root;
  Icarus `reg bool` and binary `~&`/`~|` are marked as Icarus extensions.

Investigated and left open: `$bits(instance.type)` (a type named through a
module instance, not LRM); class-parameter constants (`C#(5)::v`) in module
constants; base classes whose type actual is a class (`extends P #(item)`);
mailbox `try_get` inside a fork; real conversion wider than 64 bits;
unpacked-structure member assignments in `always_comb`; members of
structure arrays inside structures (`e.d[1][0].b`); ivtest `sv_packed_port1`
and `sv_packed_port2` (z on undriven variable bits, Icarus behaviour),
`pr985` (Icarus process order), `br988` (a bare nested block in a generate
block).

Measurement: the full six-suite run b51a (wip51e) with its failures and
the fast set rerun on wip51f (fast-set cases all pass). Losses in b51a
were fixed before the rerun: a configuration with several design cells
named by `--top` (the runner elaborates each cell), a static function's
output formal copied to a virtual-interface member (the batch 51
static-output change is reverted), a typedef cycle through a structure
member (now `FSIM-ELAB-SVTYPE-003`), and a negated literal compared with a
wider unsigned value (`-1` now applies at the context width, 11.6.1, in
constants and at run time). Verilator `t_timing_unset1` to `3` expect
Verilator's `--timing` flag error and are now out of scope.

ctest: the `vhdl`, `systemverilog` and `compliance` labels fail only the 11
known tests.

From this batch on, counts are passed/in-scope: cases the runner skips as
out of scope can never close and are left out of the denominator (owner,
2026-10-12). The batch 50 column is recomputed on batch 51's scope.
Excluded cases:

- ivtest 86: Icarus diagnostics in gold output (44), Icarus system tasks
  (19), Icarus syntax (5), iverilog options and test types (18).
- Verilator 174: SystemC, PLI/VPI or DPI harness (84), `$c` (32), C++
  harness (24), lint-only (21), missing top file (8), non-standard
  behaviour (5).
- nvc 215: nvc-specific test kinds (shell, VHPI, cover, wave, mixed, Tcl,
  relaxed, plugin; 209), nvc options (4), named work libraries (2).
- sv-tests, VESTs, VHDL Compliance-Tests: none.

| Suite | Batch 50 (wip50i) | Batch 51 (wip51f) |
|---|---:|---:|
| VESTs | 3337/3648 | 3337/3648 |
| nvc regress | 643/1267 | 647/1267 |
| VHDL Compliance-Tests | 33/72 | 33/72 |
| ivtest | 2345/2740 | 2377/2740 |
| Verilator test_regress | 1287/2241 | 1311/2241 |
| sv-tests | 1413/1497 | 1416/1497 |

### Batch 50: class functions in SimIR, enumerated randomization, IEEE reals, file descriptors

Fixture: `sv_language_details_batch50.sv` (passes on xsim).

- Class functions, virtual and `super.` calls, static class functions and
  constructors run as lowered SimIR subroutines; value-container class
  properties are stored per object (8.7, 8.10, 8.20). See
  `docs/class-function-lowering-plan.md` (dedicated project, steps 1 to 4).
- Randomization: interval bounds for 17- to 64-bit random variables (solver
  step 1; Verilator `have_solver` cases run), enumerated properties limited
  to their literals and enumeration literals in constraints, `randomize(v)`
  of a non-`rand` property, unqualified `randomize()` and
  `c.constraint_mode()` in methods, `dist` items without a weight,
  multi-index constraint `foreach` (18.4 to 18.11).
- Reals follow IEEE 754 (inf and NaN); x and z convert to zero; `%d` of a
  real rounds (6.12, 21.2.1.3).
- File I/O tolerates zero, closed and unknown descriptors; STDOUT/STDERR
  descriptors; `$fopen` failure returns 0 under the project root (21.3).
- Out-of-range fixed-array and string accesses, unknown and negative
  delays, two-state copy-out, signed parameters without a range,
  parameter-dependent ranges, unsized fill literals on ports,
  nested-class static access, `++` in constant functions (7.4.6, 6.16,
  9.4.1, 13.3, 6.20.2, 5.7.1, 8.23, 11.4.2).
- Parsing: `$stop` in functions, compound genvar steps, null generate
  items, repeated time declarations and `` `timescale `` precedence,
  based-literal delays, `const`/`var`/`virtual` block declarations,
  defaultless array parameter ports.
- Runner: ivtest `diff=` output files; a `$stop` counts as the expected
  failure of a Verilator `fails` case.

Out of scope: Verilator `t_timing_osc` (`timeunit` after other items),
`t_sys_readmem_bad_*` (Verilator error policy); ivtest `cast_int`
(inf to x), `pr556`/`pr2138979d` (unseeded `$random` sequence),
`pr2486350` (`$simtime`).

Investigated and left open: the `%t` group (integer `%t` arguments need
the module time unit at run time; `$monitor` prints the current time for
every `%t`; about 9 ivtest cases); packed arrays of packed structures;
a genvar shadowed by a later module declaration of the same name
(`genvar_compressed`, `genvar_inc_dec`); real/integer conversion wider
than 64 bits; inline constraints naming the caller's locals; container
randomization (class-lowering step 5); `%E`/`%G` upper case; a signed
4-state output formal copied to a wider actual (`implicit_cast11`).

Measurement (failures of b50a and the fast set rerun with wip50i; VHDL
suites unchanged, fast-set VHDL cases all pass). The batch 49 column is
the batch 49 measurement; five of the batch 50 gains (four Verilator
semaphore cases and ivtest `pr2794144`) were credited to batch 49.

ctest: the `vhdl`, `systemverilog` and `compliance` labels fail only the 11
known tests. `fsim.runtime.fused_staging_failure` (not labelled) fails on
a fused static cohort check unrelated to this batch.

| Suite | Batch 49 (wip48) | Batch 50 (wip50i) |
|---|---:|---:|
| VESTs | 3337/3648 | 3337/3648 |
| nvc regress | 643/1482 | 643/1482 |
| VHDL Compliance-Tests | 33/72 | 33/72 |
| ivtest | 2292/2826 | 2345/2826 |
| Verilator test_regress | 1222/2415 | 1287/2415 |
| sv-tests | 1411/1497 | 1413/1497 |

### Batch 49: binds, real conversions, untyped parameters, virtual tasks, preprocessing

Fixture: `sv_language_details_batch49.sv` (passes on xsim).

- SystemVerilog and Verilog:
  - compilation-unit `bind` directives, bound instance arrays, and
    callables of other instances reading those instances' parameters
    (23.11, 23.8); tasks of constant-indexed instance-array elements;
  - rows of multidimensional unpacked arrays assigned from rows, patterns
    and conditionals (7.6, 10.9.1);
  - real/integral conversion in conditionals (including an unknown
    condition), assignment patterns, port connections, argument copy-in
    and copy-out, compound assignments, `++`/`--` expressions and
    `inside` (6.12.2, 11.4);
  - untyped parameters sized and typed by their override values, type
    parameters and typedefs bound to `real`, typedefs of `string`
    (6.20.2, 6.20.3, 6.18);
  - virtual task dispatch (8.20), inherited nested classes, string
    formals forwarded between class methods;
  - `time` operands as 64-bit integrals, compound-assignment signedness
    (6.11, 11.8.1), `%v` in `$monitor`;
  - module `mailbox`/`semaphore` initializers, `` `include `` from a macro,
    display-argument name checks, constant-function `$display`/`case`;
  - `fsim compile --preprocess-only` for sv-tests preprocessing cases.
- Class `randomize()` enumerates at most 2^22 values per property and
  samples unconstrained wide properties directly; constrained wide
  properties still need the constraint solver (a dedicated project).
- The corpus runner marks Verilator `$c`, Verilator non-standard checks
  and Icarus extensions as skipped.

Out of scope: Verilator `t_langext` (procedural assignment to a net),
`t_math_pow3` line 79, `t_package_export`, `t_func_purification` (`$c`);
ivtest `pr1528093` (`%` on real), `br1029a/b` (`-0` from `%0d` of a
negative real), `implicit_cast13` (needs `__ICARUS__`), `pr985` (order of
two time-0 initial blocks).

Investigated and left open: switch outputs with L/H strengths
(`cmos`, `pr544`, `switch_primitives`); `parameter signed p = 4'd1`
without a range (1 bit instead of the value's width); a runtime format
string (`$display(fmt, ...)`, `$sformatf(fmt, ...)`, about 8 cases);
a class declared in a generate block (`t_gen_class`); `Pkg::Cls a = new`
and `const` class handles at module scope; forward typedefs of `string`;
empty positional constructor arguments (`new(5, , "x")`).

Measurement (failures of b50a and the fast set rerun with wip48;
VHDL suites unchanged, fast-set VHDL cases all pass). Verilator
`t_bitsel_struct` and `t_paramgraph_bits_iface_typedef` were false passes
(wrong widths). The four Verilator semaphore cases and ivtest
`pr2794144` were fixed after wip48 and checked by targeted reruns.

ctest: the `vhdl`, `systemverilog` and `compliance` labels fail only the 11
known tests. A full unlabelled run also fails application tests on delta
ordering, plusargs diagnostics and static source gates (for example
`named_events`, `resolution`, `core_simulation`,
`cache-corruption-isolation-contract`); `plusargs` fails the same way with
the batch 48 binary, and the others were not compared with a baseline.

| Suite | Batch 48 (b49) | Batch 49 (wip48) |
|---|---:|---:|
| VESTs | 3337/3648 | 3337/3648 |
| nvc regress | 643/1482 | 643/1482 |
| VHDL Compliance-Tests | 33/72 | 33/72 |
| ivtest | 2263/2826 | 2292/2826 |
| Verilator test_regress | 1204/2415 | 1222/2415 |
| sv-tests | 1410/1497 | 1411/1497 |

### Batch 48: nested containers, package arrays, out-of-range selects, instance-array slices

Fixture: `sv_language_details_batch48.sv` (passes on xsim).

- SystemVerilog and Verilog:
  - reads and writes of elements of nested containers (dynamic arrays of
    fixed arrays, associative arrays of associative arrays, queues of
    queues), with defaults for out-of-range nested reads (7.4.6);
  - package unpacked arrays indexed at run time and shared by modules;
    package structure members (`P::s.x[N]`) (26.3);
  - constant bit- and part-selects outside the declared range read X
    (11.5.1);
  - `$bits`/`$size`/bound queries as constants, on strings and container
    elements, and `$bits` of typedefs in parameter overrides (20.6, 20.7);
  - `super.x`, package-qualified and chained enumeration methods, and
    for-loop variables of their declared type (8.15, 6.19.5, 12.7.1);
  - function output arguments written from an `if` condition and through
    virtual interface members; `real` outputs converted to integral
    actuals (13.4, 6.12.2);
  - constant functions reading their own name (13.4.1);
  - instance arrays connected to slices of wide actuals (23.3.3.5);
  - typedef'd local dynamic arrays; `FSIM-SV-CLASS-INHERIT-013` for type
    parameters conflicting across extended interface classes (8.26.6.2).

Out of scope: Verilator `t_enum_type_methods` line 71 prints an
enumeration with `%s` as its name ("non-standard but majority");
`t_package_export` reads `pkg11::PARAM1`, which `pkg11` imports but does not
export (26.6); `t_foreach` iterates a string, which IEEE 1800-2023 adds.

Investigated and left open: nvc `bounds23` needs a run-time check that a
VHDL composite value's length matches its target; a static task's output
formal is reset on each call instead of keeping its value (13.3.1);
an array of unpacked structures with a constant pattern initializer
(`5.10-structure-arrays`); `$bits` of a typedef reached through an
interface instance (`types.cc_index_t`); 97-bit parameter arithmetic
(`t_param_wide_io`).

The full run b49 used wip42 (batch 48 before its last two fixes). Its
losses, fixed in batch 49: ivtest `constfunc13` (a `$display` in a constant
function was reported as an error, and a case with mixed signed and
unsigned items compared signed), `struct_packed_sysfunct` and Verilator
`t_struct_packed_sysfunct` and `t_lparam_assign_iface_typedef3` (`$bits` of
a member or of a structure from another specialization was folded with the
wrong width), `br_gh62` and `pr2528915` (selects of bit-selects and of
undeclared names were accepted). Verilator `t_func_purification` calls
Verilator's `$c` in a function that a folded `if` condition no longer
skips; it is out of scope.

Corpus (passed/total, unique case ids; against b48):

| Suite | Batch 47 | Batch 48 |
|---|---:|---:|
| VESTs | 3337/3648 | 3337/3648 |
| nvc regress | 639/1482 | 643/1482 |
| VHDL Compliance-Tests | 33/72 | 33/72 |
| ivtest | 2246/2826 | 2263/2826 |
| Verilator test_regress | 1175/2415 | 1204/2415 |
| sv-tests | 1409/1497 | 1410/1497 |

### Batch 47: class-scoped types, process handles, floating inputs, out-of-range writes

Fixture: `sv_language_details_batch47.sv` (passes on xsim, which dispatches
every call through a handle of an interface class with two extended
interfaces to the first prototype; the fixture calls through single-interface
handles instead).

- SystemVerilog and Verilog:
  - types named through a class scope (`C#(T)::item_t`) specialized with
    the selection's actuals in every declaration context, and type aliases
    found through bases and interface classes (8.25, 8.26);
  - methods of every interface class an interface class extends, multiple
    interface-class inheritance from IEEE 1800-2012, and typed constructors
    with named arguments (8.8, 8.26.3);
  - process methods on computed receivers (`process::self().srandom(7)`)
    and `p.status` in name form (9.7); `constraint_mode` and `rand_mode`
    as statements through member chains (18.8, 18.9);
  - unconnected net input ports float at `z` or the pull value (23.3.3);
  - partly out-of-range part-select writes and run-time bit writes into
    array elements (11.5.1); intra-assignment delays to array elements;
  - `new[n]` local dynamic-array initializers, keyed constant associative
    initializers, type-keyed structure patterns (7.5.1, 10.9.2);
  - packed dimensions in array queries and multidimensional packed widths
    (20.7);
  - `$` for `int` parameters; specparams as local parameters and
    `FSIM-SV-SEM-419` for overridable parameters naming one (6.20.5);
    ranged specparams and `+=>`/`-=>` paths;
  - `FSIM-ELAB-SVEVENT-011` for a named event passed to a non-event formal;
    radix formats of strings; hierarchical event triggers; bare
    `` `pragma protect ``;
  - the time-zero late start narrowed to processes that read a settling
    signal.

Out of scope: Verilator `t_constraint_dist_randmode_unsup` expects an
unsupported-feature error for legal `rand_mode` use, which fsim now accepts.

Investigated and left open: fork children inside automatic tasks do not
share the task's automatic variables; instance-array port slicing; a net
delay plus a continuous assignment delay is one stage, not two; `$typename`;
class-scoped value parameters; assertions in generate blocks; `%E`/`%G`;
wide (>64-bit) parameter arithmetic; `job[i].status`; object
`get_randstate`/`set_randstate`; unconstrained `rand int` randomization
enumerates the whole domain.

The full run b48 used wip41 (88eda230). Its losses: Verilator
`t_constraint_dist_randmode_unsup` (out of scope above), and nvc `bounds23`
and sv-tests `8.26.6.2--parameter_type_conflict_unresolved`, investigated
in batch 48.

Corpus (passed/total, unique case ids; against b47):

| Suite | Batch 46 | Batch 47 |
|---|---:|---:|
| VESTs | 3337/3648 | 3337/3648 |
| nvc regress | 639/1482 | 639/1482 |
| VHDL Compliance-Tests | 33/72 | 33/72 |
| ivtest | 2232/2826 | 2246/2826 |
| Verilator test_regress | 1155/2415 | 1175/2415 |
| sv-tests | 1391/1497 | 1409/1497 |

### Batch 46: interface arrays, interface port parameters, seeded random, time-zero order

Fixture: `sv_language_details_batch46.sv` (passes on xsim).

- SystemVerilog and Verilog:
  - multidimensional interface instance arrays (`ifc grid[2][3]()`) and
    their elements as port actuals (23.3.3.5);
  - non-ANSI interface ports declared in the body as `Iface.mp port;`, and
    modport import/export of callables declared later in the interface
    (25.5, 25.7);
  - `process::self()`, `status()` and the `process::state` constants in
    module processes (9.7);
  - queue slices `q[a:b]`, string queue `push_*`/`pop_*`, index-keyed
    array patterns (`'{2: v, default: d}`), string literals in unpacked
    byte-array patterns (7.10, 10.9.1);
  - hierarchical `force`/`release` targets (10.6.2);
  - specparams read as constants outside the specify block (6.20.5);
  - parameters of the interface instance bound to an interface port in
    constant expressions (`localparam p = port.PARAM;`, 25.10);
  - a static class function called as a statement outside a class (13.4.1);
  - `$random(seed)`, and the Annex N `uniform` helper now takes the seed's
    high 23 bits, so every `$dist_*` value matches other simulators;
  - at time zero, an `initial` block that only waits on time and reads a
    net starts one delta after the other processes (4.7 leaves the order
    open);
  - `` `include `` of an absolute path, and the ivtest root on the include
    path.

Out of scope: ivtest `$is_signed`, `$ivlh_*`, `$countdrivers`, `$sizeof`,
`$fopenr`, `$simparam` and `$abstime` (Icarus and Verilog-AMS extensions),
and `pv_wr_vec*_nb_ec` (they read a nonblocking event-controlled update in
the same active region, which xsim also rejects).

Investigated and left open:
- `uvm_resource_db#(virtual X)::set` and `uvm_config_db` calls from module
  scope: the resolver cannot select the library's parameterized classes.
- A delayed continuous assignment through a real parameter delay
  (`assign #Delay y = a`, `br_gh732`) still starts at z.
- Instance arrays connected to a wide actual (`and_gate u[0:3] (out, a, b)`)
  connect the low bits to every instance instead of a slice each.

The full run b47 used wip39. Its loss `6.20.5--specparam_inv` is fixed
after it (an overridable parameter naming a specparam is now
`FSIM-SV-SEM-419`). Verilator `t_timing_timescale` stays lost: it relies on
one `initial` block running before an `always` block that its `#(delay)`
reads, an order the late start changes and 4.7 leaves open; starting such
blocks on time instead loses three ivtest cases.

Corpus (passed/total, unique case ids; against b46):

| Suite | Batch 45 | Batch 46 |
|---|---:|---:|
| VESTs | 3337/3648 | 3337/3648 |
| nvc regress | 636/1482 | 639/1482 |
| VHDL Compliance-Tests | 33/72 | 33/72 |
| ivtest | 2213/2826 | 2232/2826 |
| Verilator test_regress | 1107/2415 | 1155/2415 |
| sv-tests | 1390/1497 | 1391/1497 |

### Batch 45: queues, ports, modport expressions, loop forms

Fixture: `sv_language_details_batch45.sv` (passes on xsim).

- SystemVerilog and Verilog:
  - `$` indexes the last element of a queue in reads and writes
    (`q[$]`, `q[$-1]`); out-of-range, negative or unknown `delete` and
    `insert` indexes do nothing, popping an empty queue returns the default
    value, and a write one past the end appends (7.10);
  - an output port connected to a concatenation (`.o({a, b})`, 23.3.3);
  - `%h` and `%o` print `X` or `Z` for a digit with some unknown bits
    (21.2.1.3);
  - sensitivity lists may hold constants and expressions beside signals
    (9.4.2);
  - ANSI port defaults (`output bit clk = 0`) and generated variable
    initializers execute (`FSIM-SV-UNSUPPORTED-010` and `-011` are
    retired); Verilog-2005 rejects `parameter` in a generate block
    (`FSIM-VERILOG-SEM-015`, IEEE 1364-2005 12.4);
  - for loops with several initializers and any step assignment; `foreach`
    with a skipped dimension or a selected collection (12.7);
  - modport expressions `.alias(signal)` and actuals that select a modport
    (`.port(bus.mp)`, 25.5);
  - `@(posedge vif.member)` on a virtual interface (25.9);
  - `$fscanf` into an array element or select (21.3.4.3);
  - elaboration severity tasks as module items, labels on immediate
    assertions, attributes before statements, `this.super`, min:typ:max
    expressions, string line continuation, a select of a concatenation,
    and clocking drives with a cycle delay (`cb.v <= ##2 e`);
  - a whole-array continuous assignment between different unpacked shapes
    is an error again (7.6).

Out of scope: Verilator `t_module_reserved_keyword` (a lint of C++
reserved words) and ivtest `sv_queue_parray`, `sv_queue_real`, `sv_queue_vec`,
`sv_immediate_assert`, `sv_immediate_assume`, `always_comb_warn`,
`always_latch_warn` and `pr1866215` expect Icarus's own warning and error
text in their gold output.

Investigated and left open:
- Packed arrays of packed structures (`pair_t [3:0] p;`) need element
  layouts across parsing, widths and selects.
- Multidimensional interface instance arrays.
- Real arithmetic yielding infinities or NaN (division by zero) fails in
  several layers that require finite values.
- Time-zero order: running initial blocks after continuous assignments in
  the active region gained two cases and was not adopted.

The full run b46 used wip37. Its one ivtest loss, `parameter_in_generate1`
(a Verilog-2005 negative case that used to fail on the generated
initializer), is fixed after it by `FSIM-VERILOG-SEM-015` and checked with
a targeted rerun.

Corpus (passed/total, unique case ids; against b45):

| Suite | Batch 44 | Batch 45 |
|---|---:|---:|
| VESTs | 3337/3648 | 3337/3648 |
| nvc regress | 633/1482 | 636/1482 |
| VHDL Compliance-Tests | 33/72 | 33/72 |
| ivtest | 2198/2826 | 2213/2826 (2212 in b46) |
| Verilator test_regress | 1075/2415 | 1107/2415 |
| sv-tests | 1385/1497 | 1390/1497 |

### Batch 44: run-time delays, whole arrays, disable, keyed parameters

Fixture: `sv_language_details_batch44.sv` (passes on xsim).

- SystemVerilog and Verilog:
  - a continuous assignment or gate with run-time rise, fall and turn-off
    delays (`assign #(d1, d2) y = a;`, `buf #(rise, fall)`) selects the
    delay by the new value (10.3.3, 28.16);
  - a continuous or nonblocking assignment of a whole unpacked array
    (`assign x = y;`, `a <= A[i];`), with any index ranges of equal size;
    `always_comb` and continuous assignments reading a whole array wake
    when an element changes (10.3, 10.4.2);
  - an unpacked subarray read through an unknown or out-of-range index
    gives the default value (7.4.6);
  - `disable` of a named block, named fork or task that another process
    (or a sibling fork branch) executes, by a simple or hierarchical name
    (9.6.2);
  - container declaration initializers: an unpacked concatenation for a
    fixed-size array (`int a[3] = {1, 2, 3}`), `new[n]` and `new[n](init)`,
    and a string literal for an unpacked array of bytes (10.10, 7.5.1,
    5.9);
  - a structure parameter given as a keyed assignment pattern
    (`'{a: 8, default: 0}`) in constant contexts: localparams, widths,
    typedefs and interface parameters that use its members (10.9.2).

Investigated and left open:
- A disabled process that waits on a delay resumes at the block's end when
  the delay expires, not at once (process wakes cannot be cancelled).
- Hierarchical references to another instance's parameters
  (`m.lpbit`, `io.types.ABits`).
- Time-zero order of initial blocks and continuous assignments.

The full run b45 used wip36 (the commit's code). Its one loss,
`sv_array_cassign_fail3` (a whole-array continuous assignment between
different unpacked shapes was accepted), is fixed in batch 45.

Corpus (passed/total, unique case ids; against b44):

| Suite | Batch 43 | Batch 44 |
|---|---:|---:|
| VESTs | 3337/3648 | 3337/3648 |
| nvc regress | 633/1482 | 633/1482 |
| VHDL Compliance-Tests | 33/72 | 33/72 |
| ivtest | 2179/2826 | 2198/2826 |
| Verilator test_regress | 1035/2415 | 1075/2415 |
| sv-tests | 1383/1497 | 1385/1497 |

### Batch 43: pull primitives, constraint inheritance, file reads, empty items

Fixture: `sv_language_details_batch43.sv` (passes on xsim).

- SystemVerilog and Verilog:
  - `pullup (net);` is an unnamed instance whose terminal is `net`; only a
    strength keyword in the parentheses makes them a strength (28.10);
  - a derived class's inherited constraints validate when the base class
    lies outside the catalog projection being checked;
  - reading a file opened for writing returns EOF and records the error for
    `$ferror` instead of stopping the simulation; `$ungetc` pushes back a
    character whatever the file's mode (21.3);
  - a lone `;` is an empty compilation-unit or package item (A.1.11);
  - an escaped identifier ends only at white space and may contain
    backslashes; one whose characters form a simple identifier names that
    identifier (`\abc` is `abc`, 5.6.1);
  - nonblocking assignments in `always_comb` and `always_latch` are legal
    and execute (`FSIM-SV-SEM-013` is retired);
  - `return;` ends a void function; `$clog2` of a constant lowers to its
    value; `$typename` of a built-in type keyword is that keyword, and such
    keywords are not undeclared identifiers;
  - `$shortrealtobits` converts a real argument to shortreal;
  - `%t` of a time variable follows `$timeformat`'s width; `%t` of a real
    time value is written in whole time units;
  - `%s` of `c ? "yes" : "NO"` is the packed value as wide as the wider
    literal;
  - writing a zero byte or an out-of-range index to a string element
    leaves the string unchanged (6.16);
  - a port typed by a type parameter takes its signedness;
  - a structure-typed parameter given as an assignment pattern, a name of
    another such parameter, or an override reads correctly in procedural
    code, including member selects (`P.m`);
  - a module-level `specparam`; a real `specparam` value;
  - a one-bit type (`bit`, `logic`, `reg`) as a streaming slice size;
  - the JIT accepts a string element write of any source width;
  - a local declared `static` in an automatic function or class method
    keeps its value across calls, and such a function is not folded as a
    constant; the class-method executor evaluates `++`/`--` of a local;
  - a concrete class that inherits a pure constraint it does not override
    is reported (18.5.2), a check the projection validation used to make by
    accident.
- VHDL: `use p.all` where `p` is a package generic makes the members of
  the package instance bound to it visible.
- Runner: sv-tests assertions may use `in` and `not in`, as Python's
  `eval` does.

Out of scope: ivtest `bool1` and `compare_bool_reg` (`reg bool`),
`binary_nand` and `binary_nor` (binary `~&` and `~|`), and
`pr1787423`, `pr1787423b`, `pr2834340`, `pr2834340b` (a pull primitive
with several terminals; 28.10 allows one). All are Icarus extensions.

Investigated and left open:
- UVM `run_test` does not complete in the class-HIR host interpreter for
  either UVM release (2020.3.1 stops at the package function `uvm_init`,
  1.2 at a `void'(...)` statement); about 34 sv-tests UVM cases depend on
  it, through `uvm_resource_db#(virtual X)`.
- Time-zero order: ivtest and Verilator cases that read continuous
  assignments from an initial block at time 0 expect the assignments to
  settle first, as xsim and Icarus do. Deferring initial blocks a delta
  also defers the variable initializers bundled with them, so it was not
  adopted.
- Structure-typed parameters in constant contexts (`localparam int A =
  cfg.A`, widths derived from them) need keyed assignment patterns in both
  constant evaluators; the Verilator `t_lparam_dep_iface*` cases depend on
  it.

The full run b44 used wip35. The pure-constraint check, static locals and
the class-method `++`/`--` came after it and were checked with targeted
reruns (sv-tests 18.5.x, Verilator `t_var_static` and `t_class_static`)
and the labelled ctest suites.

Corpus (passed/total, unique case ids; against b43/b43v):

| Suite | Batch 42 | Batch 43 |
|---|---:|---:|
| VESTs | 3337/3648 | 3337/3648 |
| nvc regress | 630/1482 | 633/1482 |
| VHDL Compliance-Tests | 33/72 | 33/72 |
| ivtest | 2157/2826 | 2179/2826 |
| Verilator test_regress | 1023/2415 | 1037/2415 (1035 in b44) |
| sv-tests | 1358/1497 | 1384/1497 (1383 in b44) |

No case that passed before fails.

### Batch 42: compilation-unit checks, tagged unions, VHDL predefined operations, timeout ordering

Fixtures: `vhdl_predefined_min_max_to_string.vhd` and
`sv_event_iff_and_local_parameters.sv` (passes on xsim). xsim gets array
MINIMUM/MAXIMUM and TO_STRING of character-enumeration arrays wrong (it
prints positions), so the VHDL fixture follows nvc's `predef1` and
`predef4` expectations instead.

- SystemVerilog and Verilog:
  - duplicate generate and named-block names (`FSIM-SV-SEM-416`),
    non-constant attribute values (`-417`), and compilation-unit typedefs
    that name undefined types or forward types never defined (`-418`);
  - `$unit` functions and tasks report undeclared identifiers;
  - event controls with an `iff` guard (`@(e iff g)`); several guarded
    events in one control are reported (`FSIM-SV-UNSUPPORTED-048`);
  - tagged unions: `void` members, `tagged X` without a value, `if (e
    matches p)`, and a run-time error for reading a member other than
    the current tag;
  - `disable soft`, `randomize() with (idlist)` and `.randomize() with
    {...};` as a statement;
  - procedural `parameter`/`localparam` without a type are integers;
    `output var` ports are variables; a procedural assignment to a port
    declared as a net is reported;
  - gate-array terminal widths, `$fdisplay(fd)`, `$ungetc` of a string
    literal, `%s` of packed values, and `$sscanf`/`$fscanf` corner cases.
- VHDL:
  - predefined MINIMUM and MAXIMUM of scalars, of one-dimensional arrays
    (including null arrays and qualified aggregates), and TO_STRING of
    REAL with DIGITS or FORMAT, of TIME with UNIT, of physical values and
    of character-enumeration arrays;
  - string literals of character-enumeration arrays, `?=` with
    metavalues (`'X'`), and logical operators with one BIT/STD_ULOGIC and
    one array operand;
  - arrays of constrained STRING subtypes before VHDL-2008;
  - a process resuming from `wait for` sees the driver updates of that
    time (IEEE 1076-2008 14.7.5.3); it used to run before them. The VITAL
    timing checks now compare event times, not delta cycles;
  - sec, min and hr design time resolutions;
  - analysis checks: resolution function return type (`VHRESOLVE-002`),
    `out` access formals before 2008 (`VHACCESS-034`), positional
    component port actual types (`VHPORT-006`), out-port reads before
    2008, record aggregate and slice checks.
- Test sources fixed to legal code: an ambiguous enumeration literal in a
  type conversion, unconstrained STRING variables in a VHDL-2008 test,
  NATURAL values assigned to UNSIGNED without NUMERIC_STD, and a forced
  bit-select with a variable index.

Out of scope: Verilator `t_langext_2`, `t_langext_4`,
`t_langext_2012ext`, `t_langext_2017ext`, `t_langext_2023ext` (procedural
assignment to a non-ANSI output port with no data declaration, which is a
net; IEEE 1800-2017 23.2.2.3) and `t_math_pow6` (a non-constant
initializer on a net output port). xsim rejects all six.

Still failing outside the corpora: `fsim.application.resolution`
(the restored switch topology names a different control signal) and the
`wildcard_nba` expectation in `procedural_assignments`, which depends on
initial-block order; both were hidden behind earlier failures.

The full run b43 used wip32; the VHDL suites were rerun as b43v on wip33,
which adds the timeout ordering, the VITAL event times, arrays of STRING
elements and the JIT `?=` fix. The tagged-union run-time error came after
wip32 and passes in a targeted rerun.

Corpus (passed/total, unique case ids; against b42):

| Suite | Batch 41 | Batch 42 |
|---|---:|---:|
| VESTs | 3297/3648 | 3337/3648 |
| nvc regress | 617/1482 | 630/1482 |
| VHDL Compliance-Tests | 33/72 | 33/72 |
| ivtest | 2144/2826 | 2157/2826 |
| Verilator test_regress | 1028/2415 | 1023/2415 |
| sv-tests | 1337/1497 | 1358/1497 (1357 in b43) |

No case that passed in b42 fails, except the six out-of-scope Verilator
cases above.

### Batch 41: undeclared identifiers, VHDL analysis checks, interface members

Fixtures: eleven `analysis_errors` cases (`case_variable_choice`,
`procedure_in_expression`, `real_loop_bound`, `nonstatic_type_range`,
`physical_unit_range`, `type_mark_bound`, `ambiguous_bit_literals`,
`alias_of_type`, `loop_parameter_scope`, `nonstatic_port_actual`,
`aggregate_choice_range`), `vhdl_unconstrained_constants.vhd` (passes on
xsim) and `sv_interface_member_connections.sv` (passes on xsim).

- SystemVerilog and Verilog:
  - undeclared simple identifiers are rejected outside implicit-net
    contexts (`FSIM-SV-SEM-411`);
  - primitives drive nets only before SystemVerilog (`-412`);
  - unsized constants in Verilog concatenations are rejected (`-413`);
  - undeclared non-ANSI ports are rejected (`-414`);
  - `$signed`/`$unsigned` of a real is rejected (`-415`);
  - Verilog port and data ranges must match;
  - `$` ends of `inside` ranges work;
  - interface members can be named hierarchically in assertions, task
    outputs and virtual-interface class properties.
- VHDL:
  - about twenty analysis checks from the VESTs `analyzer_failure` cases
    (see the notes);
  - two drivers on an unresolved INTEGER signal are reported;
  - an unconstrained array constant can be indexed;
  - literal actuals of unconstrained array formals have their index range.
- Test sources fixed to legal SystemVerilog: an assignment to a task
  formal from module scope, `$dimensions` of that formal, and an
  undeclared type-generic actual (`int` now).

The full run used wip29. Six VESTs cases it lost to the new checks are
fixed after it and pass in a targeted rerun:
- `ch_04_ch_04_01`: an ambiguous anonymous range now counts only types
  visible from the array;
- `tc2448` and `tc2501`: an anonymous range's placeholder INTEGER index
  is no longer checked;
- `tc740`, `tc759` and `tc760`: an aggregate initializer no longer sets
  the constant's width.

Out of scope: ivtest `br_gh788` needs Icarus's `-gno-io-range-error`
extension; IEEE 1364-2005 12.3.3 forbids its declarations.

Corpus (passed/total, unique case ids; b42 = wip29, against r1):

| Suite | Regression 1 | Batch 41 |
|---|---:|---:|
| VESTs | 3266/3648 | 3297/3648 (3303 with the six fixes) |
| nvc regress | 613/1482 | 617/1482 |
| VHDL Compliance-Tests | 31/72 | 33/72 |
| ivtest | 2129/2826 | 2144/2826 |
| Verilator test_regress | 1022/2415 | 1028/2415 |
| sv-tests | 1337/1497 | 1337/1497 |

No other case that passed in r1 fails in b42.

### Regression batch 1: ctest regressions from the LRM batches

Fixtures: `vhdl_record_defaults_and_resolution.vhd` and
`sv_buffer_gates_z_input.sv` (both pass on xsim).

The `vhdl`, `systemverilog` and `compliance` ctest labels had 41
failures. Most came from analysis checks added by earlier batches that
our own test designs violated, and some from simulator regressions.

Simulator fixes:

- A record signal's STD_LOGIC_VECTOR element started at 'X' instead of
  'U' (since batch 34): a record default now stays nine-valued when an
  element is.
- `buf` and `bufif` drive x for a z input (IEEE 1800-2017 28.5, 28.6);
  they passed z through. Matches xsim, including the delays.
- A resolution function's parameter must be a one-dimensional
  unconstrained array (IEEE 1076-2008 4.6).
- A port actual qualified by a subtype of the formal's type is accepted
  (the check compared type mark names).
- Generic map actuals of type, package and subprogram generics are not
  flagged as type names (`FSIM-ELAB-VHNAME-004`); external name paths are
  not checked as expanded names (`FSIM-ELAB-VHNAME-014`).
- VHDL-2019 signals and variables with an unconstrained array subtype
  and an initial value take its constraints.
- `use lib.unit;` naming no design unit is reported (`FSIM-ELAB-PKG-001`).
- STD.TEXTIO has no WRITE for SEVERITY_LEVEL, FILE_OPEN_KIND or
  FILE_OPEN_STATUS (`FSIM-ELAB-VHTEXTIO-009`).
- Two codes reused by later batches with new meanings were renumbered
  (`FSIM-ELAB-GENERIC-011`, `FSIM-ELAB-SVAGG-008`); retired codes moved to
  the compatibility registry; the catalog documents VHNAME-014/015 and
  FE-VHCONFIG-001.

Test sources fixed to legal VHDL (the owner chose fixing tests over
keeping non-LRM extensions): signals passed to `variable` formals,
reserved words as identifiers (`label`, generate labels equal to
generics), slices of two-dimensional arrays, negative BIT_VECTOR bounds,
`f()` calls, missing IEEE context clauses and component declarations,
NATURAL assigned to UNSIGNED, an access-type signal, an unconstrained
VHDL-2008 variable, OTHERS in an unconstrained qualified aggregate, and
a declaration between the generic and port clauses. Expectations that
changed with LRM-correct behaviour were updated (TIME includes negative
values; `use open` leaves an instance unbound; a forced net drives its
switch network; module path delays reject short pulses; the VITAL path
delay applies at 0 ns; newer analysis codes replace bounded-runtime
codes), each checked against xsim where it is a simulation result.

Housekeeping: test initializers for a field added in batch 29 (the
`fsim_runtime_tests` and `fsim_llvm_tests` binaries did not compile),
SPDX notices and `.license` companions for files added without them,
and stale counts in the closure-audit and Windows ABI checks.

Still failing (not LRM): the native region-kernel admission tests
(`systemverilog-scheduling-witnesses.llvm-O0/O2`, `native-region-route`,
`native-frontier-random-dag`) fail with the v4 qualification binary too;
`specify` parses a runtime-state layout changed by Engine v4 Phase 1;
`vhdl_projected` (native component member count) and two runtime
optimizer tests (`fsim.runtime` static cohorts,
`aggregate_proxy_leaf_write_differential`) need separate investigation.
`uvm-closure-audit` and `source-package-manifest` fail only on an
untracked local file. Known gap: with a partial force of one bit of a
net, a bit-select of that bit reads Z instead of X before the force.

Corpus (passed/total, unique case ids; r1 = wip25, against c3):

| Suite | Cleanup 3 | Regression 1 |
|---|---:|---:|
| VESTs | 3266/3648 | 3266/3648 |
| nvc regress | 612/1482 | 613/1482 |
| VHDL Compliance-Tests | 31/72 | 31/72 |
| ivtest | 2124/2826 | 2129/2826 |
| Verilator test_regress | 1022/2415 | 1022/2415 |
| sv-tests | 1337/1497 | 1337/1497 |

No case that passed in c3 fails in r1.

### Cleanup batch 3: behavioural merges

Fixture: `vhdl2019_integer_vectors.vhd`.

- VHDL function and procedure overload resolution share one
  actual-to-formal predicate. Procedures now also check the nominal type,
  array shapes, record formals and unspecified-type inference; functions
  now also reject abstract literals for TIME formals, character literals
  that do not belong to the formal's type, and scalar BIT or STD_ULOGIC
  actuals for array formals.
- `%p` in `$display`, `$sformatf` and `$fwrite` shares one helper for
  unpacked values, strings and enumeration names.
- INTEGER_VECTOR elements are 64 bits under VHDL-2019, matching INTEGER
  (IEEE 1076-2019 5.2.3.1). A signal aggregate of INTEGER_VECTOR was
  rejected before (`FSIM-ELAB-VHARRAYAGG-002`), and default values used
  the 32-bit INTEGER'LOW.
- Static values of record and array elements go through one dispatcher
  (REAL, CHARACTER, arrays, then other values). Subtypes derived from
  REAL, CHARACTER and STRING are recognised through their whole chain;
  qualified character literals and character constants are followed in
  both places.
- The compile-time name check runs the validator in a names-only mode
  instead of running every type check and discarding the results.
- Assignment lowering caches whether an update target needs
  re-evaluation (it was recomputed up to 22 times per statement).
- The validator's 14 hand-written "types differ" tests use one helper.

ctest: the `vhdl` (19 failures) and `systemverilog` (22 failures)
labels fail the same tests with the c2 and c3 libraries. Those failures
predate the cleanup (for example `FSIM-ELAB-VHNAME-004` on `'ascending`
and on an instance label, and `FSIM-ELAB-VHARRAYSEL-005`); the next
batch fixes them.

Corpus (passed/total, unique case ids; c3 = wip24, against c2):

| Suite | Cleanup 2 | Cleanup 3 |
|---|---:|---:|
| VESTs | 3261/3648 | 3266/3648 |
| nvc regress | 611/1482 | 612/1482 |
| VHDL Compliance-Tests | 31/72 | 31/72 |
| ivtest | 2124/2826 | 2124/2826 |
| Verilator test_regress | 1022/2415 | 1022/2415 |
| sv-tests | 1337/1497 | 1337/1497 |

No case that passed in c2 fails in c3; VESTs gains tc440, tc473, tc474,
tc485 and tc486, and nvc gains one case.

### Cleanup batch 2: shared helpers

No behaviour change is intended. Repeated code is replaced by shared
helpers:

- `frontend::is_real_scalar_kind` (shortreal, real, realtime) for 39
  three-way comparisons and two file-local helpers, across the frontend,
  elaboration, runtime and application.
- One `compiled_source_span` in elaboration for twelve file-local
  copies, two inline conversions and `Lowerer::hir_source_span`. All
  elaboration diagnostics now carry the macro expansion stack, which
  only four of the copies did.
- One table of STD.STANDARD TIME units for six copies.
- `lowercase_ascii`, `lowercase_ascii_copy` and
  `equal_ignoring_ascii_case` for 34 in-place lowercase transforms,
  eight file-local identifier comparisons and nine inline comparisons.
- `simple_name_of` for four file-local functions and five lambdas that
  strip a name to its last `.` or `:` segment.
- `Lowerer::hir_vhdl_range_left` and `hir_vhdl_range_right` for 38
  copies that fold a range bound that is still an expression.
- The SystemVerilog coverage parser and randsequence use the shared
  `split_top_level` and `find_top_level`.
- The VHDL parser parses local package instantiations with one member
  (five copies) and skips balanced parentheses in lookahead with one
  member (three copies).

Corpus (passed/total, unique case ids; c2 = wip23, against c1):

| Suite | Cleanup 1 | Cleanup 2 |
|---|---:|---:|
| VESTs | 3261/3648 | 3261/3648 |
| nvc regress | 611/1482 | 611/1482 |
| VHDL Compliance-Tests | 31/72 | 31/72 |
| ivtest | 2124/2826 | 2124/2826 |
| Verilator test_regress | 1022/2415 | 1022/2415 |
| sv-tests | 1337/1497 | 1337/1497 |

No case that passed in c1 fails in c2.

### Cleanup batch 1: drifted copies and dead code

Fixture: `sv_pattern_files_and_production_defaults.sv` (xsim does not
support randsequence; the `%p` part passes on xsim).

This batch follows an audit of the code added by batches 1 to 40. It
fixes bugs where copies of the same logic had drifted apart, and
removes dead code:

- Processes generated while lowering deferred SystemVerilog statements
  (fork branches, assertion threads) now keep their program block's
  reactive region and owner. Concurrent statements, processes and
  generated processes are published by three shared members of
  HierarchyBuilder instead of seven inline copies. A generated-block
  process that uses a hierarchical reference is no longer remembered as
  a template.
- The VHDL procedure frame cache keys element constraints with their
  direction, as function frames do (one shared key builder).
- OTHERS choices in static VHDL aggregates are matched exactly rather
  than by substring.
- randsequence production formals with defaults that contain commas
  split at top level.
- `$fwrite` with `%p` writes an enumeration value by name, as `$display`
  and `$sformatf` do.
- Removed: a repeated waveform type check, an unreachable record
  constant branch, an unreachable `real_vector` element branch, an
  unused binding and a duplicate expression lookup. Compile-time
  analysis diagnostics use the shared span conversion.

Not changed: the audit also listed a 32-bit default `integer_vector`
element under VHDL-2019, which would change behaviour and moves to
cleanup batch 3. Its report that class `new` passes string actuals
differently from methods did not reproduce (literal, variable and
concatenation actuals, with and without defaults, all bind).

Corpus (passed/total, unique case ids; c1 = wip22, against w123 for the
VHDL suites and w119 for the SystemVerilog suites):

| Suite | Batch 40 | Cleanup 1 |
|---|---:|---:|
| VESTs | 3261/3648 | 3261/3648 |
| nvc regress | 611/1482 | 611/1482 |
| VHDL Compliance-Tests | 31/72 | 31/72 |
| ivtest | 2124/2826 | 2124/2826 |
| Verilator test_regress | 1022/2415 | 1022/2415 |
| sv-tests | 1337/1497 | 1337/1497 |

No case that passed at batch 40 fails in c1.

### Batch 40: package instances, blocks, literal overloads and analysis checks

Fixture: `vhdl_package_instances_and_blocks.vhd` (passes on xsim).

- Design-unit instantiation of a generic package declared in the same
  file, expanded into an ordinary package (constant, type and subprogram
  generics); package instantiations in package declarative regions.
- An abstract literal without a unit does not match TIME formals.
- `label : name;` component instances, configuration specifications and
  `use open` in blocks.
- Analysis checks: labels and design-unit names as map actuals, block
  configuration labels, and name checks at compile for architectures with
  instances.

Corpus (passed/total, unique case ids; w123 = wip21 for the VHDL suites;
the SystemVerilog suites are unchanged from batch 39, which this batch
does not touch):

| Suite | Batch 39 | Batch 40 |
|---|---:|---:|
| VESTs | 3247/3648 | 3261/3648 |
| nvc regress | 609/1482 | 611/1482 |
| VHDL Compliance-Tests | 29/72 | 31/72 |
| ivtest | 2124/2826 | 2124/2826 |
| Verilator test_regress | 1022/2415 | 1022/2415 |
| sv-tests | 1337/1497 | 1337/1497 |

No VHDL case that passed at batch 39 fails in w123.

### Batch 39: port conversions, bindings, generics, matching operators, packed elements

Fixture: `vhdl_matching_units_and_bindings.vhd` (xsim does not support
subprogram instantiation; with the generic function written as a plain
function the fixture passes on xsim).

- Port actuals that are conversion function calls use an adapter with
  the formal's type; arrays of INTEGER ports bind (directly and through
  conversions).
- Binding indications with their own generic or port maps bind
  components that do not declare those generics; static REAL generic
  actuals; REAL generics in real arithmetic.
- Entity-prefixed expanded names must denote entity declarations
  (`FSIM-ELAB-VHNAME-014`, fixing the batch 38 regression on tc1014).
- Generic subprograms in standard order; operator-symbol interface
  function designators parse.
- User physical unit names as primaries; matching ordering operators;
  procedure overload resolution by character literal and scalar/array
  kind of the actuals.
- Variables of `INTEGER_VECTOR` and `REAL_VECTOR` without initializers.
- SystemVerilog: indices into an unpacked array of packed arrays select
  packed elements correctly (reads and writes).
- VUnit stand-in: single-return protected functions.

Corpus (passed/total, unique case ids; w120 = wip18 for VESTs, nvc and
VHDL Compliance-Tests, w119 = wip17 for ivtest, Verilator and sv-tests):

| Suite | Batch 38 | Batch 39 |
|---|---:|---:|
| VESTs | 3223/3648 | 3247/3648 |
| nvc regress | 602/1482 | 609/1482 |
| VHDL Compliance-Tests | 19/72 | 29/72 |
| ivtest | 2124/2826 | 2124/2826 |
| Verilator test_regress | 1020/2415 | 1022/2415 |
| sv-tests | 1337/1497 | 1337/1497 |

No case that passed at Batch 38 fails in w119 or w120.

### Batch 38: string methods, timescales, return conversions, individual association

Fixtures: `sv_string_methods_and_updates.sv`,
`sv_package_timescale_and_returns.sv`, `vhdl_records_arrays_and_values.vhd`
(all pass on xsim) and `vhdl_individual_association.vhd` (xsim does not
support individual association in subprogram calls; the rest of the
fixture passes on xsim with those calls written out).

- Class methods that return strings, compound updates of variably indexed
  unpacked elements, virtual calls through pure virtual prototypes.
- Elements of multidimensional arrays of arrays and of record constants;
  `'VALUE` of integer and enumeration types.
- UVM user components specialize and construct, with report queries
  answered from the report policy; `run_test()` itself is still not
  executed (see the gaps queue).
- Verilog-2005 shifts take the context width; `$printtimescale` and the
  `$q_*` tasks are accepted in Verilog-2005.
- Package and `$unit` subroutines use their own time unit for `$time`
  and `$printtimescale`.
- `return` converts between real and integral function results.
- VHDL STRING element assignment; individual association of formal
  subelements in procedure calls; `out` actuals that are record elements
  or static elements of two-dimensional arrays; expanded names whose
  prefix is an enclosing label or the design unit.

Corpus (passed/total, unique case ids; w116 = wip14 for VESTs, nvc,
VHDL Compliance-Tests, ivtest and Verilator, w115 = wip13 for sv-tests,
plus targeted wip15 reruns for the VHDL items after wip14):

| Suite | Batch 37 (w114) | Batch 38 |
|---|---:|---:|
| VESTs | 3213/3648 | 3222/3648 (w116 3213 plus 9 targeted) |
| nvc regress | 597/1482 | 600/1482 (w116 598 plus 2 targeted) |
| VHDL Compliance-Tests | 19/72 | 19/72 |
| ivtest | 2109/2826 | 2124/2826 |
| Verilator test_regress | 1018/2415 | 1020/2415 |
| sv-tests | 1329/1497 | 1337/1497 |

No case that passed in w114 fails in w115 or w116.

### Batch 37: forces, bidirectional connections, strengths and constructors

Fixture: `sv_force_switches_and_strengths.sv` (passes on xsim without its
`tran`, which xsim does not support; the `tran` family matches the ivtest
golds exactly).

- `force` keeps its expression applied and overrides procedural
  `assign`; a release lets the assignment take effect again.
- `inout` ports connected to bit-selects, part-selects and
  concatenations join the selected nets through bidirectional switches.
- `tran`/`rtran`/`tranif*` are solved only by the switch network (no
  feedback drivers), at start-up as well, with forced nets, supply
  reduction and ambiguous strengths per IEEE 1800-2017 28.12-28.13.
- `%v` strength output.
- Class constructors receive string arguments, also through
  `super.new`, and `super.new` works across separate compilations.
- Virtual function calls through pure virtual prototypes and interface
  class methods.

Corpus effect (interpreter; w111 plus targeted reruns): ivtest +34
(2105), Verilator +5 (1014), sv-tests +1 (1329).

### Batch 36: analysis checks, STD.ENV imports and TEXTIO TEE and FLUSH

Fixtures: `vhdl_environment_tee_and_flush.vhd` (passes on xsim) and seven
analysis-error fixtures (`undeclared_procedure`, `guard_assignment`,
`integer_guard`, `character_to_bit`, `undeclared_choice`,
`scalar_to_record_element`, `reserved_unit_name`; xsim rejects each).

- Procedure calls that name no visible procedure are rejected at analysis
  (`FSIM-ELAB-VHPROC-014`, naming the procedure).
- STD.ENV `stop` and `finish` called by simple name after a use clause.
- Guard checks: assignments to the implicit GUARD signal and integer
  guard expressions.
- `all` as a design unit name; CHARACTER control literal names typed as
  CHARACTER; case choices checked as names; selected element names typed
  from their prefix object.
- TEXTIO `TEE` and `FLUSH`.

Corpus effect (interpreter; w108 plus targeted nvc reruns): VESTs +10
(3198), nvc +3 (`stop1`, `issue718`, `file6`).

### Batch 35: tables, case choices, 'SUBTYPE and final procedures

Fixtures: `vhdl_tables_cases_and_subtypes.vhd` and
`sv_final_procedure_termination.sv` (both pass on xsim).

- Multidimensional constants of BIT or STD_ULOGIC given by positional
  nested aggregates (`and_table('L', '1')`).
- Character literal case choices of BIT selectors.
- `obj'subtype` as a subtype indication.
- `'LENGTH`, `'LEFT`, `'RIGHT`, `'LOW` and `'HIGH` of runtime-sized STRING
  objects and STRING formals.
- String function names without actuals, signal attributes with their
  signal's subtype, and lexicographic ordering of BIT_VECTOR operands of
  different lengths.
- SystemVerilog: `$finish` and `$stop` in final procedures; named events in
  generate blocks. The generate parser no longer loops on an unsupported
  item; ivtest `scoped_events` grew to 12 GB before.

Corpus effect (interpreter; w106 plus tc350, w104): VESTs +8 (3189),
nvc +4 (588), Verilator +4 (1009), ivtest +2 (2071).

### Batch 34: element constraints from actuals, labels and attribute specifications

Fixtures: `vhdl_formal_element_constraints.vhd` and
`sv_block_labels_and_class_parameters.sv` (both pass on xsim).

- Unbounded array formals take their actual's element constraints; frames
  are specialized by them, and `x(i)'range` in the subprogram is bounded.
- Element constraints in index ranges, constant selections, static signal
  initializers, and dynamic-index and member element subtypes.
- Constant selections of VHDL arrays with multi-bit elements: an
  INTEGER_VECTOR element of a record in an array (`t(1).x(2)`) is read and
  written at its own offset. Defaults of standard vectors inside records
  are `'LEFT`.
- Attribute specification checks (`FSIM-ELAB-VHATTR-016`).
- Character literal indices of BIT, CHARACTER and STD_ULOGIC dimensions,
  and multi-index calls of enumeration-indexed tables.
- SystemVerilog block labels (`name: begin ... end: name`), and classes
  whose parameter has no default.

Corpus effect (interpreter, w102 and the sv-tests rerun): VESTs +5 (3181),
nvc +8 (584), sv-tests +6.

### Batch 33: aggregates, imports, standard vectors and element constraints

Fixtures: `vhdl_qualified_aggregates_and_imports.vhd` and
`vhdl_element_constraints.vhd` (both pass on xsim).

- TIME array elements are 64 bits, including user `time_vector` types and
  locals with literal index bounds.
- Character literal elements of CHARACTER arrays.
- Use clauses naming a library (`use work.all`), a library unit (`use
  work.pkg;`), or the contents of a package made visible by an earlier use
  clause.
- Qualified aggregates of process- and subprogram-local types.
- Array ordering with operands of different lengths and aggregate
  operands.
- INTEGER_VECTOR, BOOLEAN_VECTOR, TIME_VECTOR and REAL_VECTOR: aggregates,
  indexing and element defaults.
- Records with access-typed elements; an assignment to such an element no
  longer replaces the whole record.
- VHDL-2008 element constraints on arrays and records (`T(r)(r2)`,
  `T(open)(r2)`, `R(e(r))`, nested and through named subtypes).
- The compiled engine leaves VHDL resolution programs to the interpreter.
  Batch 31's `vhdl_resolution_functions` fixture failed in the compiled
  engine.

Corpus effect (interpreter, w98 plus later fixes): VESTs +42 (3176), nvc
+16 (576), VHDL Compliance-Tests +1 (19). nvc issue616 was lost: it is a
negative test that now elaborates (see the notes). Of the 49 nvc cases
blocked by element-constraint syntax, 7 pass. The rest fail on element
constraints taken from actuals, `'element`, external names, package
instantiation, and other gaps.

### Batch 32: entity constants, array orderings, names and literal checks

Fixture: `vhdl_names_and_orderings.vhd` (passes on xsim).

- Constants in entity declarative parts; signal-class function formals
  (passed by value, with a signal-actual check, `FSIM-ELAB-VHCALL-003`).
- Ordering operators on equal-length discrete arrays.
- Range attributes as array index constraints and integer type ranges;
  non-discrete index subtypes rejected (`FSIM-ELAB-VHARRAY-012`).
- Indexed and sliced user attribute values; slices of slices; indexed
  record elements of objects (`v.re1(1)`).
- `'ACTIVE` of a signal element; BOOLEAN-array port bindings.
- Class methods have automatic lifetime by default, and a class task call's
  retained callee body is not allocated as the caller's static locals (the
  UVM `run_test` frame now lowers; UVM classes still fail to specialize).
- Literal checks: underlines between decimal digits, based digits below
  the base (`FSIM-VHDL-LEX-005`); a scalar attribute value cannot be
  indexed (`FSIM-VHDL-SEM-126`); entity subprograms are not entity
  statements for the passive check.

Corpus effect (interpreter, w96 plus tc1076 fixed after it): VESTs +31
(3135), nvc +4 (560). VESTs tc1127 (an out-of-range index of a slice at
analysis) is accepted again: it failed before only because chained slices
did not parse.

### Batch 31: VHDL resolution functions and default values

Fixture: `vhdl_resolution_functions.vhd` (passes on xsim).

- General VHDL resolution functions. A function other than `return a or b`
  / `and` now runs as a per-signal resolver program over the drivers'
  values, indexed from the low bound of the formal's index subtype. It runs
  after each update's driver commits, and once at start for the initial
  value. Records, integers, enumerations and BIT/STD_ULOGIC element types
  are supported.
- Default values of records and arrays without an initial value come from
  each scalar subelement's subtype (NATURAL 0, POSITIVE 1, integer range
  left, enumeration left, REAL'LEFT), for signals and variables. REAL
  objects start at REAL'LEFT.
- Physical literals of user physical types evaluate statically; signal
  initializers given by CHARACTER, REAL or other statically valued constants
  are accepted.
- Member names of signals of resolved record subtypes.

Corpus effect (interpreter, w94): VESTs +20 (3104), nvc +10 (556),
vhdl-compliance unchanged (18); no regressions. Still open: guarded `bus`
and `register` signals (disconnection changes the driver count) and arrays
of resolved elements (7 VESTs, nvc guard4), and record signals initialized
from record constants of 2-D arrays (6 VESTs).

### Batch 30: character arrays, predefined bounds, analysis checks, SV array forms

Fixtures: `vhdl_character_arrays_and_bounds.vhd` and
`sv_array_sizes_and_methods.sv` (both pass on xsim).

VHDL values:
- String literals of user arrays of CHARACTER (`type t is array (...) of
  character`), compared and passed as packed values.
- `'LEFT`, `'LOW` and `'HIGH` of the predefined REAL and TIME type marks.
  `'LAST_EVENT` and `'LAST_ACTIVE` before any event return TIME'HIGH. The
  TIME image of TIME'LOW no longer overflows at coarse resolutions.
- Dynamic indexing of objects whose subtype is a named subtype of a
  predefined vector (`subtype byte is bit_vector(0 to 7)`).

VHDL analysis checks:
- Lexical conformance of subprogram bodies to their declarations before
  VHDL-2008, both in packages and in one declarative region
  (`FSIM-VHDL-SEM-125`).
- Linkage ports: reads and updates outside port associations
  (`FSIM-ELAB-VHPORT-004`) and signal-attribute reads (`-003`). A linkage
  formal accepts actuals of any mode.
- Range bounds of different enumeration types, in array index constraints
  and loops; slice bounds of a type other than the prefix's index type
  (`FSIM-ELAB-VHRANGE-001`).
- Scalar `'LEFT`/`'RIGHT`/`'LOW`/`'HIGH` given an argument
  (`FSIM-ELAB-VHATTR-015`).

SystemVerilog:
- `#1step` delays.
- Built-in array methods without parentheses (`q.delete;`, `a.sort;`).
- Unpacked dimensions given by a constant size expression (`[N+1]`,
  `[2**W]`); `**` in static integer evaluation.
- Wildcard associative arrays (`int a[*]`).
- Run-time `$clog2`.
- A class specialization's parameter read through a handle (`obj.P`).
- `obj.randomize();` and the other built-in randomization methods as
  statements.
- Task names in the `FSIM-ELAB-HIR-001` message for task calls that cannot
  be lowered.

Corpus effect (interpreter): VESTs +40 (3048 to 3084 in w92, plus tc308 and
tc1076 restored after that run); nvc +1 (546); vhdl-compliance +1 (18);
ivtest +6 (2069); sv-tests +3 (1280); Verilator `#1step` cases
(`t_delay_1step`, `t_clocking_input_default`, `t_clocking_input_0_delay`).
No regressions in nvc, vhdl-compliance, ivtest or sv-tests against r72/w91.
Verilator was not rerun.

### Batch 29: subprogram array formals, slice aliases, strings, NBA struct members

Fixtures: `vhdl_aliases_and_array_formals.vhd` and
`sv_nonblocking_struct_members.sv` (both pass on xsim).

VHDL subprograms:
- Formals of unconstrained array types whose elements are wider than one bit
  (arrays of INTEGER, REAL, records) take the actual's packed value and index
  range; the captured range is valid when the actual holds a whole number of
  elements per index, so `v'range`, `v'length` and `v(i)` work in the body.
- Functions return arrays and records of wide elements as packed values.
- Procedure calls capture the index ranges of unconstrained array actuals
  (one frame per distinct range), which they did not before.
- STRING formals of functions that do not return STRING are runtime strings,
  with the actual's static index range (its constraint, or 1 to the length
  of a static value).

VHDL names and values:
- Object aliases of slices and elements (`alias a : string(4 downto 1) is
  s(1 to 4)`, `alias c : character is s(3)`): the target is kept as the
  alias's expression; the alias's own subtype gives its index range;
  whole-object aliases of STRING variables and constants follow their object
  (`FSIM-ELAB-VHALIAS-003` reports an element count that differs from the
  slice's).
- Slices and elements of runtime STRING values (`s(1 to 4) = "Hell"`,
  `s(4) = 'l'`), with CHARACTER as the element subtype so `'H'` and `'L'`
  are not taken as STD_ULOGIC literals.
- Integer literals with an exponent (`2E9`) in static evaluation, and
  physical type ranges given by static expressions.
- Configurations: a deferred constant's value is used for generate index
  specifications; a generic of the configured entity takes its default;
  range attributes of objects and subtype names select index ranges.

SystemVerilog: nonblocking writes to members of unpacked structures and of
unpacked arrays of structures (`arr[0].foo <= 0`). `WriteContainerObjectElement`
carries an optional member path and updates the member when the write is
published, so several member writes in one time step compose.

Elaboration: arrays indexed by a CHARACTER subtype get their layout from the
character bounds; the predefined VHDL-2008 vectors `integer_vector`,
`real_vector` and `time_vector` have signal layouts.

Corpus effect (interpreter, against the r72 baseline): VESTs +45
(3048/3648, no regressions), nvc +20 (545, from the w90 run; the final nvc,
ivtest, Verilator and sv-tests reruns were stopped by memory pressure and
are taken by the next baseline), Verilator struct-member NBA cases
(`t_nba_struct_array`, `t_struct_array_assignment_delayed`).

### Batch 28: typed-file images, constant bounds, signal initial values, analysis checks

Fixture: `vhdl_constant_bounds_and_file_images.vhd` (passes on xsim).

Typed VHDL files (`file of T` for any scalar, record or array `T`) now use a
line-oriented text image codec, which is the representation of the VESTs
iofiles: one element per line; scalars as their images (decimal integers,
REAL in standard form, enumeration literals, physical values with a unit,
TIME in fs, a standalone CHARACTER as `'c'`); composites as their scalar
parts separated by single spaces; strings as their characters. READ of an
unconstrained array type takes a LENGTH and reads as many elements as the
line holds. A fixed-shape element is scanned from the file, so lines are
not limited by the 4096-byte runtime string size; writes emit each part as
it is formatted. The codec replaces the decimal/hexadecimal per-line format
of batch 6 (files written by older builds are not readable).

VHDL elaboration and runtime:
- An unconstrained array constant takes its index range from its value
  (`'LENGTH`, `'LEFT`, `'RIGHT`, `'ASCENDING`; a string literal or positional
  aggregate starts at the index subtype's left bound); the specialization's
  integral evaluator derives `'LENGTH` from literals, positional aggregates
  and parameterless functions that return them, so bounds checks such as
  `constant c2 : real_vector(1 to c1'length) := (6 elements)` are reported.
- `'LEFT`, `'RIGHT`, `'LOW`, `'HIGH`, `'ASCENDING` of floating subtypes;
  array attributes through an access value whose designated subtype is
  constrained (`head'low`).
- Signal initial values: negative TIME values (TIME now has the full 64-bit
  range; DELAY_LENGTH keeps 0 to TIME'HIGH), records with REAL, CHARACTER
  and array elements, record constants and generics, STRING generics, REAL
  generics in array aggregates.
- Aggregates: a scalar element's packed layout range (TIME's 63 downto 0)
  is no longer taken as its value range (2 us in a record aggregate failed
  a range check).
- Configurations: `use entity lib.e` without an architecture selects the
  most recently analyzed one; `label : comp;` without maps instantiates a
  declared component.
- Workspaces whose objects use different subsets of the compiler-supplied
  VHDL packages (one with NUMERIC_STD, one without) link with the largest
  set; before, the second object failed with `FSIM-ART-VHDEP-001`.

VHDL parsing: based real literals (`16#F.F#E1`, `5:1234.4321:E-10` with
the colon replacement character), `E+n` exponents, the optional `signal`
keyword and `linkage` mode in port clauses, component ports without a mode,
empty parentheses after a name (`FSIM-VHDL-PARSE-303`).

VHDL analysis-time checks (reported by `fsim compile`): index counts and
types of indexed names (`FSIM-ELAB-VHARRAYSEL-005`), object alias subtypes
(`FSIM-ELAB-VHALIAS-001`/`-002`), slice assignment lengths
(`FSIM-ELAB-VHASSIGN-004`), return values of another type
(`FSIM-ELAB-VHRETURN-001`), subaggregate lengths (`FSIM-ELAB-VHAGG-017`),
record aggregate OTHERS and multiple choices (`FSIM-ELAB-VHAGG-018`),
characters that are not elements of BIT or STD_ULOGIC in string literals
(`FSIM-ELAB-VHAGG-019`), statements of passive entities
(`FSIM-ELAB-VHPASSIVE-001`), waveforms whose delays do not increase
(`FSIM-ELAB-VHWAVE-001`), record element names used as type marks in the
record (`FSIM-ELAB-VHRECORD-003`), type marks not visible at their
declaration (`FSIM-ELAB-VHTYPE-006`, split from `-001`), signal actuals of
variable formals and the reverse, scalar values assigned to composite
targets, OTHERS aggregates passed to unconstrained formals, signal
attributes and sensitivity-list reads of `out` ports before VHDL-2008
(`FSIM-ELAB-VHPORT-003`), non-static sensitivity-list names
(`FSIM-ELAB-VHSENS-001`), `.all` of a non-access value
(`FSIM-ELAB-VHACCESS-033`), relational operators between two aggregates
(`FSIM-ELAB-VHAGG-020`), a second port clause. At elaboration, an
instantiated component must be declared (`FSIM-ELAB-VHCOMP-016`) and map
actuals must be values (instantiating architectures are not elaborated by
`fsim compile`, so these do not fire there).

Configurations: generate index specifications accept deferred constants,
generics with defaults, range attributes of objects and subtypes, and
subtype names (`for G(dumy'range)`, `for G(sub)`).

Earlier in the batch: BIT_VECTOR constants are materialized as read-only
signals for indexed reads; process-local array constants; `'SIMPLE_NAME`,
`'PATH_NAME` and `'INSTANCE_NAME`; the UVM library path of the corpus
runner is absolute (the batch 27 relative path failed 8 UVM cases).

Runner: a VESTs test whose top entity has a configuration elaborates the
configuration, which supplies the component bindings.

Corpus effect (interpreter, full VHDL suites and reruns of the Batch 27
baseline's UVM failures): VESTs +172 (3003/3648), nvc +14 (525), VHDL
Compliance-Tests +1 (18), sv-tests +8 (the UVM library path); about 195
closed. Nine VESTs negative cases that were rejected only because the
`signal` keyword in a port clause was not parsed are now accepted: reads of
`linkage` ports (5, not recorded in the HIR) and instance checks in
instantiating architectures (4, which `fsim compile` does not elaborate).

### Batch 27: VHDL analysis checks, distinct scalar types, generate interfaces, UVM library

Fixtures: `vhdl_time_literals_and_scalar_types.vhd` and
`sv_generate_interfaces_and_upward_names.sv` (both pass on xsim).

VHDL analysis-time checks (reported by `fsim compile`), most from the
VESTs non-compliant tests:
- Names: a type, subtype, component, package instance or block/generate
  label used as a value (`FSIM-ELAB-VHNAME-004`), in more value positions
  (index expressions, aggregates, qualified and converted operands, range
  bounds of type definitions, variable assignment targets).
- Integer and floating type definitions (`type T is range ...`) are distinct
  types for the type checks, while still executing as INTEGER or REAL
  subtypes.
- Assignments: variable targets that are signals, constants, functions or
  generate parameters (`FSIM-ELAB-VHASSIGN-003`); signal assignments to a
  slice of a constant; waveform values and object initial values of another
  scalar type.
- Calls: procedure calls whose actuals no visible procedure accepts by
  count, name or scalar type (`FSIM-ELAB-VHCALL-002`); function call
  actuals of another scalar type.
- Aggregates: mixed positional and named associations, repeated choices,
  element values of another type, multidimensional element associations
  that are not aggregates, OTHERS in a qualified unconstrained aggregate
  (`FSIM-ELAB-VHAGG-012` to `-016`).
- Case statements: a non-discrete selector, choices of another type, static
  choices outside the selector's subtype, string choices of another length
  (`FSIM-ELAB-VHDLCASE-007` to `-010`).
- Declarations: unconstrained signals and variables, unconstrained composite
  elements before VHDL-2008, access types designating files, files of
  multidimensional arrays, generics and ports of file types, attributes of
  access or file types, INTEGER ranges beyond 32 bits before VHDL-2019,
  static initial values outside their range, string and bit-vector index
  bounds outside POSITIVE and NATURAL, subprograms declared in a package body
  without a body there.
- Expressions: array conversions with unrelated index types, unconstrained
  allocators, static division by zero, ordering operators on records,
  arithmetic on STRING, delays and timeouts not of type TIME, array indices
  of another type, `bit'('C')`.
- Parse and lexical rules: chained relational or shift operators, mixed or
  repeated NAND/NOR without parentheses, operator symbol calls and operator
  function arities, empty port and sensitivity lists, a second generic
  clause, architecture end names, package generics before VHDL-2008,
  underlines and negative exponents in literals, duplicate aggregate
  targets, actual port modes incompatible with formal ports (VHDL-93).

VHDL:
- The automatic time resolution also considers every TIME literal, not
  only delays, at the coarsest decimal unit that represents each exactly.
- A unit name of a user physical type (`fs` in `type t is range ... units
  fs; ...`) is that type's unit, not the TIME unit.
- Concatenation of CHARACTER values with strings (`'"' & s & '"'`).

SystemVerilog:
- Continuous and procedural assignments to members of interface instances
  declared in generate blocks, by deferring those statements until the
  block's instances exist.
- Functions of module instance array elements called by hierarchical name
  (`h.slots[0].put(5)`, also as `void'(...)`), and upward names that begin
  with an enclosing module's name (`holder.marker`).
- Classes declared at compilation-unit scope see packages imported there,
  including packages of other (mapped) libraries.
- The `.*` port check exempts interface ports.

Runner: UVM is compiled once per run into a mapped library (see "Corpora
and runner"); the UVM sv-tests subset took 45% less time with the same
results.

Corpus effect (interpreter, full VHDL suites and reruns of the Batch 26
baseline's SV failures): VESTs +154 (2831/3648), Verilator +11, ivtest +2,
nvc and VHDL Compliance-Tests unchanged, no regressions in the VHDL suites;
167 closed.

### Batch 26: TextIO, operator-symbol calls, element names, hierarchical calls

Fixtures: `vhdl_textio_and_element_names.vhd` and `sv_hierarchical_calls.sv`
(both pass on xsim).

VHDL analysis-time checks (reported by `fsim compile`):
- A selected name that names no element of its record prefix
  (`FSIM-ELAB-VHNAME-003`); an expanded type mark that names nothing outside
  STD and IEEE (`FSIM-ELAB-VHTYPE-005`).
- A file whose element is a file, access or protected type, or a composite
  containing one (`FSIM-ELAB-VHFILE-013`, `-014`, 5.5.1).
- 'STABLE, 'QUIET, 'TRANSACTION or 'DELAYED of a formal signal parameter
  (`FSIM-ELAB-VHATTR-012`, 4.2.2.2).
- Qualified expressions and type conversions whose operand cannot have the
  type (`FSIM-ELAB-VHQUAL-005`, `FSIM-ELAB-VHCONV-005`); a signal assignment
  to a target that is not a signal (`FSIM-ELAB-VHASSIGN-002`); an access
  object outside a variable (`FSIM-ELAB-VHACCESS-030`); floating loop
  bounds (`FSIM-ELAB-VHLOOP-001`); relational operators on file and
  protected values and on operands of different types.
- Parse-time: an array type mixing `<>` and constrained index ranges
  (`FSIM-VHDL-SEM-119`); an empty bit-string literal before VHDL-2008 and a
  based literal base outside 2..16 (`FSIM-VHDL-PARSE-298`, `-299`).

VHDL:
- STD.TEXTIO: TEXT files, OUTPUT (`writeline(output, l)`, `write(output,
  s)`), SWRITE, DEALLOCATE of a LINE, LINE formals of mode out and inout,
  and WRITE of TIME (with UNIT), REAL (with DIGITS), CHARACTER, BIT_VECTOR
  and other vectors (16.4).
- `T'IMAGE` of TIME values at run time, in fs.
- Function calls by operator symbol (`"and"(a, b)`, `STD.STANDARD."<"(a,
  b)`) and qualified character literals (`bit'('0')`, `boolean'(true)`).
- Attributes of indexed, sliced, selected and dereferenced names
  (`v(1)'left`, `q(1).x'length`).
- 'EVENT of a statically indexed signal element, and process sensitivity to
  signal elements (`process (vec(0))`), resume only on that element's events.
- STRING signal initial values from string literals and character
  aggregates; predefined enumeration subtypes with character bounds.

SystemVerilog:
- Calls of functions and tasks of other module instances and of generate
  blocks through hierarchical names (`u.bump(3)`, `blk.twice(5)`) (23.8).
- Rejected: a real operand in a concatenation (`FSIM-ELAB-SVCONCAT-002`), a
  zero-width sized literal (`FSIM-SV-SEM-409`), Verilog-mode input/inout
  ports declared reg, integer, time or real (`FSIM-SV-SEM-410`), a defparam
  value naming a variable, net or port (`FSIM-ELAB-DEFPARAM-005`), and a
  `.*` port with no matching parent object.

Corpus effect (interpreter, reruns of the Batch 25 baseline's failures
with the final build): VESTs +148, ivtest +13, Verilator +12, nvc +12, VHDL
Compliance-Tests +1, sv-tests unchanged; 186 closed. Per the batch rule, the full
corpus and ctest were not run before the commit; the run from the archived
Batch 26 binary is the next batch's baseline and regression check.

### Batch 25: VHDL names, calls and attributes, formal conversions, generate references

Fixtures: `vhdl_attributes_and_conversions.vhd` and `sv_generate_references.sv`
(both pass on xsim), and four more invalid units under
`tests/fixtures/compliance/analysis_errors/` (xsim rejects each).

VHDL analysis-time checks (reported by `fsim compile`):
- A simple name used as a value that denotes no visible declaration, such
  as a statement label, a design unit or a record element name
  (`FSIM-ELAB-VHNAME-002`, 12.3).
- A function call whose actuals no visible function accepts, by count or by
  formal name (`FSIM-ELAB-VHCALL-001`, 4.2.2.1).
- A subprogram declared in an entity, architecture or process without a
  body there (`FSIM-ELAB-VHBODY-001`, 4.2.1).
- A value of another type assigned to a scalar object, including distinct
  user integer, floating and physical types, and allocators or `null`
  assigned to non-access targets (`FSIM-ELAB-VHASSIGN-001`).
- The operator checks of batch 24 now cover arrays of user-declared types,
  physical literals (`T * 10 sec`), subtypes of REAL, and process-local
  floating types.

VHDL:
- User-defined attribute values (`gen_in'attr1`, `pt'at1.re1`) read the
  value of their attribute specification (6.7, 7.2).
- Conversion functions in the formal part of an output or inout port
  association of a declared component (`to_x(f) => s`) (6.5.7.1).
- Static REAL and CHARACTER values in signal initial values and composite
  constants (`signal s : real_vector := c;`, `(others => (others => 'q'))`).
- A user-defined operator (`function "abs" ... return level`) is no longer
  folded as the predefined one.
- A scalar type definition whose bounds are attributes or constants of a
  floating type (`range REAL'LOW to REAL'HIGH`) is a floating type.

SystemVerilog:
- Variables and instance parameters of loop generate blocks referenced by
  constant index, nested (`row[1].col[2].tap`, `inst[0].m.P`) (27.4).
- A defparam whose first name is an enclosing generate block
  (`defparam Loop[i].m.p = ...` inside `Loop`) resolves upward (23.8).

Corpus effect (interpreter, full run before the last fixes, against Batch
24): VESTs +171 (2568/3665), ivtest +6 (2050/2826), Verilator +5
(971/2415), with no regressions; sv-tests, nvc and VHDL Compliance-Tests
unchanged. 182 closed. Later fixes in the batch (predefined enumeration
subtypes, protected-type methods, VITAL designs, the scope of the name
check) were checked with ctest and targeted reruns; the next batch's corpus
run is its regression check.

### Batch 24: VHDL analysis-time checks, predefined operators, overload visibility

Fixtures: `vhdl_operator_overloads.vhd` (passes on xsim), and five invalid
units under `tests/fixtures/compliance/analysis_errors/` that `fsim compile`
must reject with a named code (xsim rejects each at analysis).

Analysis-time checks (IEEE 1076-2008 13.1):
- `fsim compile` elaborates each VHDL architecture it analyzes on its own
  and reports, at analysis, the elaboration diagnostics that identify a
  violated analysis rule (type of a condition, report or severity
  expression, operator operand types, index and range types, selections,
  qualified expressions, aggregates, access assignments). Diagnostics that
  come from missing design context (generic values, bindings) or from
  bounded-runtime limits are not reported there. An architecture that needs
  generic values, or that instantiates anything, is skipped, so compile time
  of the benchmark designs is unchanged. `FSIM_NO_ANALYSIS_ELABORATION=1`
  turns the check off.
- Predefined adding, multiplying, `mod`, `rem`, `**`, sign and `abs`
  operators applied to operands outside their numeric type rules, when no
  visible overload applies (`FSIM-ELAB-VHOPER-001`, 9.2.4-9.2.8). Array
  operands are left to library packages.
- A sign after an adding or multiplying operator, `abs`, `not` or another
  sign (`a / -b`) must be parenthesized (`FSIM-VHDL-PARSE-297`, 9.1).
- The bounds of a range in a type definition must have the same type, and a
  scalar type definition needs integer or floating-point bounds
  (`FSIM-ELAB-VHRANGE-001`); a physical type needs integer bounds
  (`FSIM-ELAB-VHPHYSICAL-011`).
- Conditions and report expressions whose type cannot be determined get
  their own codes (`FSIM-ELAB-093`, `FSIM-ELAB-VHREPORT-003`), separate
  from a known non-BOOLEAN condition or non-STRING report expression.

Corrections found by the analysis-time check:
- A function call that matches several overloads which all return BOOLEAN
  (`std_match` with a string-literal operand) is a BOOLEAN condition.
- A constant of an unconstrained array type takes its bounds from its
  initial value, and an access assignment from a record element of an
  incompletely declared type is accepted.

Overloads and arithmetic:
- A local subprogram hides an imported one only when the two are homographs
  (12.3, 12.4): a local `"-"(character, character)` no longer hides an
  imported `"-"(pair)`.
- A record formal no longer matches an integer element actual, so
  `l.a + r.a` inside an overload of `"+"` for the record calls the
  predefined integer operator instead of recursing.
- `real ** integer` at run time, and universal_real * universal_integer
  (`2.5 * 2`) computed as REAL (9.2.7, 9.2.8).

Corpus effect (interpreter, against Batch 23): VESTs +299 (2397/3665, no
regressions), nvc unchanged (499/1482), VHDL Compliance-Tests unchanged
(16/72). The batch changes only VHDL paths, so the SystemVerilog suites were
not rerun. Compile time of the benchmark designs is unchanged, and all ten
parity designs give the same output as their frozen runs.

### Batch 23: interface ports, declarations, time-zero values, VHDL ports

Fixtures: `sv_interface_ports_and_waits.sv` and `sv_declarations_and_calls.sv`
(both pass on xsim).

Interfaces (25.5, 25.9, 25.10):
- Interface port arrays (`ifc ports[2]`) bind element by element, and their
  elements' parameters and members are reachable (`ports[i].x`,
  `$bits(ports[0].data)`).
- A non-ANSI interface port declared in the module body (`module m(c);
  ifc c;`).
- Virtual interface members are read and written through the handle, and
  `@(vif.member)`, `@(posedge vif.member)` and `wait (vif.member ...)`
  wait on the selected instance.
- A modport segment in a hierarchical member reference
  (`ifc.mp.member`).

Declarations and statements:
- Non-ANSI ports bind positionally in header order, not in the order of
  their body declarations (23.3.2.1).
- An unpacked structure local to a procedure, and bit- and part-selects of
  a structure member as assignment targets (7.2).
- `for` loops with empty initializer, condition or step (12.7.1).
- `always_ff` with several events, such as an asynchronous reset
  (9.2.2.4).
- Nets of a user-defined type (`wire byte_t w;`); a net of a class type is
  rejected (`SV-SEM-407`).
- Omitted positional call arguments (`f(, 1)`) take their defaults, and
  named arguments bind by name in constant function calls (13.5).
- A real `repeat` count rounds (12.7.2).
- A formal hides a parameter of the same name in a constant function call
  (13.4.3); constant functions assign and read packed structure members.
- Typed assignment patterns (`T'{...}`), `default clocking` blocks, and
  the `edge` event (9.4.2).
- Constant `.toupper`, `.tolower` and `.substr` of string parameters.
- Statement calls of `$fgets`, `$fscanf` and the like discard the value.

Rejected input:
- An edge event on a `real` variable (`SV-SEM-406`).

Time zero (4.7, 6.8, 10.3):
- A SystemVerilog variable's constant declaration initializer takes effect
  before any process starts, so `logic clk = 0; always @(negedge clk)`
  sees no edge at time zero.
- A net or variable whose only driver is a constant continuous assignment
  starts at the constant, so initial procedures read it at time zero. A net that a
  process waits on for any change keeps its time-zero event.
- An edge event on a bit-select or expression follows table 9-2 (9.4.2): a
  posedge leaves 0 or arrives at 1, so a net's change from Z to X at time
  zero is no edge.

Specify blocks and SDF: `$sdf_annotate` calls load their SDF file at
elaboration (`ELAB-SDF-001`); module path delays drive X, not Z, until
their first delayed value.

Preprocessor and lexing: text after `` `ifdef X``, `` `else`` and
`` `endif`` on the same line; stringification keeps the spacing of its
argument; fractional delays without a timescale (`SV-SEM-050` retired).

Performance: a long chain of nested `?:` lowers in linear rather than
exponential time (ivtest `pr3022502` elaborated in 90 seconds).

VHDL:
- An omitted port mode is `in`, and `linkage` is accepted (6.5.2).
- A resolution function in a signal's subtype indication (`signal s :
  resolve bit;`) and the `bus`/`register` signal kinds.
- An object alias without a subtype indication (`VHDL-UNSUPPORTED-054`
  retired).
- `use` clauses in a process declarative part.
- `DELAY_LENGTH` and the STANDARD.CHARACTER control literals (`NUL`,
  `DEL`, ...) in constant expressions.
- Before VHDL-2008, an input port's actual may be a conversion function of
  a name or a globally static expression (1076-1993 1.1.1.2).
- Aggregate assignment targets (`(a, b) := v;`, `(a, b) <= T'("10");`),
  sequential and concurrent; the value must be an array object or a
  qualified aggregate or string.
- An attribute specification sees attributes declared in earlier units
  (entities, packages). An attribute of a design unit is specified only in
  that unit's own declarative part (`VHDL-SEM-118`); a generic clause after
  the port clause (`VHDL-SEM-117`) and `end;` closing a process are
  rejected.

Runner: Verilator `TEST_DUMPFILE`, data files named as `t/...`, and
`--file-root`; ivtest `-Tmin/-Ttyp/-Tmax` select the delay mode.

Corpus effect (interpreter, against Batch 22): ivtest +43 (2044/2826),
Verilator +61 (966/2415), sv-tests +8 (1277/1497; UVM 3/103), nvc +19
(499/1482), VESTs +65 with 5 newly failing (2098/3665); VHDL
Compliance-Tests unchanged (16/72). 196 cases closed, 191 net. The five VESTs
cases are negative tests that had failed analysis only through the
previously unsupported port forms; they need checks not yet implemented
(static names in port maps, block specifications, guarded-signal
assignments, variable targets of signal assignments).

### Batch 22: class execution, interface typedefs, foreach, equality, constraints

Fixtures: `sv_class_construction_in_methods.sv` (passes on xsim) and
`sv_values_and_assertion_items.sv` (passes on xsim except its immediate
`cover`, which xsim 2025.2 ignores).

Classes (8.7, 8.9, 8.18, 8.25, 18.5.1, 18.6.3):
- Class methods run in the class interpreter. It gained loops, `case`,
  `break`/`continue`, task calls, `$display`/`$write`, string properties,
  container methods, the implicit function result variable, and `new`. A
  class whose methods construct objects (`static function C get();
  if (single == null) single = new; ...`) used to be dropped from the
  simulation ("specialization is not available").
- Static class-handle properties initialized by `new` are constructed
  before any process runs. Property initializers that are not literals
  (expressions, enumeration literals, `new`) run after the base
  constructor.
- Class specialization links typedefs that were spelled only by name
  (`$unit` enums used as property types). It treats a parameterized class
  named without actuals as its default specialization, and accepts type
  parameters as the types of method locals and formals.
- `local` and `protected` visibility is enforced (`SV-CLASS-028`).
  User methods named `randomize`, `rand_mode`, `constraint_mode`,
  `srandom`, `get_randstate` or `set_randstate` are rejected
  (`SV-CLASS-029`). `randc` variables in `dist`, `soft` or `solve-before`
  constraints are rejected (`SV-CLASS-030`).
- Out-of-block constraints (`constraint C::c { ... }`) at compilation-unit
  or module scope complete the class's prototype. A bare `constraint c;`
  is an implicit prototype. An undefined `extern constraint` is rejected
  (`SV-CLASS-031`, `-032`).
- Static methods called through a handle and multidimensional static-array
  properties in class expressions.

Interfaces and hierarchy (25.10, 6.18):
- `typedef ifc.data_t t;` and `localparam type t = ifc.sub.t;` through an
  interface instance or interface port (`SV-SEM-405`).
- Members of interface array elements (`ifcs[i].x`) in module statements.
- Input port actuals are lowered at their own width and then extended.

Statements and expressions:
- `foreach` over multidimensional, ascending, dynamic and associative
  arrays.
- `fork ... join_none`/`join_any` inside tasks.
- `unique`, `unique0` and `priority if`.
- Immediate `assume` and `cover`, and deferred assertions as module
  items. `global clocking` blocks.
- `==`, `!=`, `==?` and `!=?` are decided by a known differing bit even
  when other bits are unknown (11.4.5). This applies to the interpreter,
  the static kernel and the LLVM engines.
- A variable index under a constant one in a multidimensional packed
  array (`a[1][i] = v`) wrote the wrong bits.
- 2-state function and task formals receive X/Z bits as 0.
- Replication in assignment patterns (`'{2{'{3{4, 5}}}}`).
- A real bit-select index (`SVEXPR-009`) and a stream wider than its
  target (`SVEXPR-010`) are rejected.
- Expression widths of relational, `->` and `<->` results (1 bit) and of
  shifts and `**` (the left operand's width).

Simulation:
- A net driven by a delayed continuous assignment reads X, not Z, until
  the first delayed value matures, with no value change at time zero.
- `%d` of a partly unknown value prints `X` or `Z` (21.2.1.4).
- `$simtime` (ivtest) returns the time in simulation-precision units.

Preprocessor (22.3, 22.5.1, 22.11):
- A conditional directive inside a macro whose name pastes an argument
  (`` `ifdef STOP_``a1``).
- `` `resetall`` inside a design element, `` `define`` of a directive
  name, and `` `pragma`` without a name are rejected (`SV-PP-054` to
  `056`).

Runner: ivtest compile-only (`CO`) cases elaborate and pass without a
PASSED line. Modules with escaped names (`\$I178`) are valid tops and
root aliases. Verilator cases load undeclared modules from `t/` as the
driver's `-y t/` does.

Corpus effect (interpreter, against Batch 21): ivtest +64 (2001/2826),
Verilator +76 (905/2415), sv-tests +64 (1269/1497); nvc, VESTs and VHDL
Compliance-Tests unchanged. 204 cases closed, none newly failing.

### Batch 21: sequence properties, randsequence, class construction, UVM compile time

Fixtures: `sv_copies_and_constructors.sv` (passes on xsim),
`sv_randsequence.sv` and `sv_sequence_properties.sv` (xsim 2025.2 does not
simulate `randsequence` or most of these property forms; their expected
values follow from the LRM and the stimulus).

Concurrent assertions (16.7-16.14):
- Properties outside the specialized slices run on a general sequence
  engine (`verilog_parser_assertion_engine.cpp`): `not`, `if`/`else`,
  `strong`/`weak`, `|->`, `|=>`, `#-#`, `#=#`, `or`, `and`, `intersect`,
  `within`, `throughout`, `##n`, `##[m:n]`, `##[m:$]`, `[*n]`, `[*m:n]`,
  `[+]`, `[=n]`, `[->n]`, `first_match` and named sequence instances with
  arguments, and unclocked property instances. The directive's process
  advances all attempts once per clock, with each automaton state a vector
  of attempt slots, so overlapping attempts keep separate state.
- Action blocks are ordinary statements, so nonblocking writes, counters
  and `$error($sformatf(...))` work there; a vacuous success runs the pass
  action.
- Inline `@(clk) disable iff (...)` properties on `assert`/`cover` and in
  checkers. A property instance with actuals is restructured from its
  substituted tokens; the specialized slices read the declaration's own
  tokens, leaving the formals unbound.
- A second `WaitRegion` to the current region stopped the process, dropping
  later actions of the same tick.

Classes (8.3, 8.8, 8.12, 8.15, 8.17, 8.21):
- `class C extends B(args);` passes the arguments to the base
  constructor; empty class items (`;`) are accepted.
- A derived constructor without formals that calls `super.new(...)` ran
  the base class's constructor in its place.
- Typed constructors `C::new(...)` and shallow copies `new h`.
- `new` of a virtual class (`SV-CLASS-024`) and assigning a handle of an
  unrelated class (`SV-CLASS-025`) are rejected.
- `#(virtual ifc)` type actuals, and `$cast` to non-class destinations
  (enums) as a task and as a function.
- Typedefs of a specialization (`typedef C#(4) T; T::name`) are not taken
  for an unparameterized class scope.

Statements and expressions (11.3.6, 12.5, 12.7, 18.16, 18.17):
- `randcase` and `randsequence` (weights, `if`, `case`, `repeat`,
  `rand join`, `break`, `return`, production arguments).
- Assignments inside expressions (`a = (b = (c = 5))`, `(x -= 1)`).
- `default` case items without a colon; `for (var int i = ...)`; `var`
  function and task formals.
- An `always` whose only timing is a nonblocking intra-assignment control
  (`always v <= @(e) c;`) is rejected (`SV-SEM-106`): it never suspends.
- A classic function formal declared without a type takes the type of a
  later declaration (`input x; real x;`).
- `void` functions used as values (`SVFUNC-014`) and procedural
  assignments of incompatible unpacked arrays (`SVASSIGN-004`) are
  rejected; `mailbox`/`semaphore` `new` in assignments.
- White space between a literal's base and its digits (`'b 0`).

Hierarchy and packages:
- Instance arrays with C-style or parameter-dependent ranges.
- `event` declarations in packages; package `export` validation across all
  packages (`SV-SEM-399`); `always`/instances in a program, packed
  structure member defaults, and Verilog declarations in unnamed blocks are
  rejected (`SV-SEM-400` to `402`).

UVM compile time: compiling the UVM package took about six minutes and now
takes about 20 s. Workspace dependency indexing was cubic, class HIR error
checks recomputed scope containment, and each class statement copied its
scope.

Corpus effect (interpreter, against Batch 20): ivtest +55 (1937/2826),
Verilator +63 (829/2415), sv-tests +71 (1205/1497), nvc +1 (480/1482);
VESTs and VHDL Compliance-Tests unchanged. 190 cases closed. Three
negative cases that failed only because a construct did not parse now
compile, since their remaining error is not yet checked (ivtest
`sv_wildcard_import4`, sv-tests `18.17.2--if-else-production-statements_{0,2}_fail`;
see the notes).

### Batch 20: delays, memory part-selects, package variables, patterns

Fixtures: `sv_patterns_and_aggregates.sv` and
`sv_delays_packages_and_classes.sv` (both pass on xsim).

Formats, aggregates and initializers (21.2.1.7, 7.2, 10.9, 10.10):
- `%p` prints assignment patterns (`'{1,-2,3}`, `'{a:3,s:"z"}`, enum
  names, quoted strings, nested arrays) through a new
  `FormatContainerPattern` operation.
- Unpacked structure members of string and real type, including singleton
  structures; container declaration initializers (patterns, `default:`,
  concatenations); unpacked array concatenation into queues and dynamic
  arrays (`AppendContainer`); `new[n]('{...})`.
- An integral value assigns to a packed structure; packed structures of
  different types are assignment compatible (6.22.3).

Delays and nonblocking writes (10.3.3, 10.4.2, 11.5.1):
- Continuous assignments and nonblocking assignments with a run-time delay
  (`assign #(period) y = x`, `q <= #d v`) use a new `WriteDelayed`
  operation; an unknown integral delay counts as zero. Modules using it
  run in the interpreter.
- Nonblocking writes to memory elements and their part-selects, with or
  without a delay or event control (`mem[0][3:0] <= @e v`): the operands
  are evaluated at once and a `join_none` branch performs the update.
- A constant part-select partially outside its vector writes only the
  bits inside it; one wholly outside does nothing.
- Procedural `assign` cannot target an array element; `defparam` cannot
  override a type parameter.

Packages, classes and names (26.2-26.6, 8.7, 8.25.1):
- Package and `$unit` variables are one design-global signal with their
  initializer, reached by explicit, wildcard or exported import and by
  `P::x`; event controls on them work.
- A class handle declared with `= new` is constructed before other
  initial processes run; string class properties are read and written from
  module code.
- `C::name` on a parameterized class outside a class is rejected
  (`SV-CLASS-023`); a constructor with non-ANSI ports is rejected.

Parsing and legality:
- Keywordless parameter port items, parameter ports without defaults,
  items after `localparam` inherit it, unpacked array parameters, empty
  positional connections, attributes inside expressions, `<->`, struct
  nets, constant-function recursion and case signedness.
- A procedural assignment to a net, including an element of a net array,
  is rejected (`SVASSIGN-003`); a part-select of a scalar memory element is
  rejected (`SVSELECT-001`); net declaration assignments on `tri`, `tri1`,
  `wand` and the other net types are continuous.

Corpus effect (interpreter): ivtest +179 (1883/2826), Verilator +39
(766/2415), sv-tests +22 (1136/1497), nvc +8 (479/1482), VESTs +2
(2037/3665); VHDL Compliance-Tests unchanged. 250 cases closed, no
regressions.

### Batch 19: real values, monitors, hierarchical parameters, events

Fixture: `sv_real_values_and_events.sv` (passes on xsim).

Real values (6.12.2, 11.3.1, 11.4.3, 11.4.13):
- Integral and real values convert when written to container elements
  (`foo[1] = i` stored the bit pattern) and when passed to function or task
  arguments, including shortreal to real formals.
- `+=`, `++` and the other compound updates of real variables and real
  array elements use real arithmetic (they used packed integer arithmetic).
- A binary operation with a real operand on either side is real (`$time /
  1000.0` displayed 0), and a real `**` is evaluated with `$pow`.
- An x/z `time` value converts to zero in a real operation instead of
  stopping the run.
- A real net (`wire real`) is unresolved; it was given 4-state wire
  resolution and rejected at load.
- Integral constant evaluation no longer runs a function with a real
  result or argument; such calls evaluate at run time.

Events, force and release (9.4.2, 10.4.2, 10.6.2):
- An event control on an element of a real array or a dynamic array
  (`@(rarr[0])`). Arrays of real elements now have a bridge signal.
- `a <= @(e) b` evaluates `b` at once and schedules the update in a
  `join_none` branch; the process no longer blocks.
  - The branch starts waiting only when the process next blocks
    (9.3.2), so an edge in between is missed.
- `release` of a variable keeps its forced value until the next assignment,
  unless a continuous assignment in the unit drives the variable.
- A force or release bit-select needs a constant index
  (`SVFORCE-001`), as xsim requires.

Display and hierarchy (21.2.3, 23.6, 13.4.5):
- `$monitor` and `$strobe` accept expressions and selects (`a + b`,
  `a[1]`, `arr[i]`): each is evaluated into a hidden signal by a generated
  `always_comb` driver. `$time`, `$stime` and `$realtime` arguments are read
  when the monitor prints.
- A hierarchical reference to an instance parameter (`u1.WIDTH`) reads the
  instance's elaborated value.
- Built-in array methods without parentheses (`q.size`, `q.pop_back`).

Declarations and legality:
- Verilog-2005 net arrays (`wire [7:0] m [0:3]`, IEEE 1364-2005 4.9).
- A net cannot be a dynamic array, queue or associative array
  (`SV-SEM-398`).
- An automatic variable cannot be a nonblocking or procedural continuous
  assignment target, or be forced (`SV-SEM-397`).
- A parameter value cannot read a variable, net or port (`SVCONST-003`).
- Continuous assignments to interface members wait for the instance
  hierarchy; generic interface ports take a modport and call methods.
- A typedef name used as an enumeration base resolves to its type.

Corpus effect (interpreter): ivtest +78 (1704/2826), Verilator +25
(727/2415), nvc +6 (471/1482), VESTs +5 (2035/3665); sv-tests and
VHDL Compliance-Tests unchanged. One Verilator case now stops as xsim
does (see `lrm-compliance-notes.md`).

### Batch 18: enumerations, real values, classes in modules, formats

Fixtures: `sv_enum_methods.sv`, `sv_type_and_class_forms.sv` (both pass on
xsim).

Enumerations (6.19):
- `first`, `last`, `next(N)`, `prev(N)`, `num` and `name`, with or without
  parentheses. Anonymous enumerations get a hidden typedef (`$enum:<first
  literal>`) so their methods and literal names resolve as for a typedef.
- Literal ranges `name[N]` and `name[N:M]` (6.19.2), and a type-name base
  with a packed dimension (`enum T [7:0]`).
- Base types must be integer atoms or vectors with at most one packed
  dimension (`SVENUM-004`). x/z values need a 4-state base, and the literal
  after one needs its own initializer (`SVENUM-005`). Values must be
  constant (`SVENUM-006`). A sized literal must have the base size.
- An integral variable, array element or function result assigned to an
  enumeration needs a cast, and so do enumeration array elements.
  Equality of two different enumeration or packed types is an integral
  comparison (11.4.5), as xsim accepts it.
- Enumeration literals declared in a class are class constants (8.23).

Real values (6.12.2, 6.24.1, 11.3.1):
- An integral value assigned to a real converts its value (it was a bit
  copy, so `r = i` gave 0), and a real assigned to an integral rounds half
  away from zero.
- `real'`, `shortreal'` and `realtime'` casts, and integral casts of real
  operands.
- An integral operand of a real operation converts. A binary operation or
  conditional with a real operand is real, as is an element of a real
  array or an untyped parameter with a real value.

Formats (21.2.1.3):
- `%e`/`%f`/`%g` take a precision (`%8.3f`) and default to six digits, as
  in C and xsim. Before, the default was 17 significant digits, and a
  precision was a parse error.
- A real conversion of an integral value converts it, in `$display`,
  `$fwrite` and `$sformatf`.

Classes:
- A class declared in a module: its properties shadow module names, so
  property writes reach the object. Its method bodies no longer create
  implicit nets in the module. A constructor no longer reports an unknown
  `constructor` type. Compound updates of class properties (`a += v`) lower
  in task bodies.
- The class executor evaluates class constants and enumeration literals.

Declarations and ports:
- `$bits` of a built-in data type (`$bits(int)`, `$bits(logic [7:0])`)
  folds to its width; a typedef name used as an operand is not an implicit
  net.
- Packed arrays of named vector types (`typedef T [3:0] U`, `T [2:0] v`).
- Non-ANSI ports and classic task arguments: `int x; output x;` in either
  order; the redeclaration checks of 23.2.2.1 and 13.3 (`SV-SEM-396`);
  `input wire T x` with a typedef.
- A SystemVerilog system service in a Verilog mode (`$bits` under
  Verilog-2005) is a warning (`SV-PARSE-350`), since IEEE 1364 leaves
  additional system services to the implementation, as iverilog does.

Runtime and runner:
- `$fopen` returns zero when it cannot open a file (21.3.1) instead of
  stopping the run.
- Runner: ivtest and Verilator expected compile failures may be rejected by
  `elaborate` (both tools' compile steps elaborate). ivtest cases run with
  `--file-root .`, a `work/` directory, and copies of the data files their
  sources name.

Corpus effect (interpreter): sv-tests +16 (1114/1497), ivtest +288
(1626/2801), Verilator +18 (703/2119), nvc +13 (465/1267), VESTs +53
(2030/3665). One ivtest port-range case, and two others that newly
completed, now expect what xsim and Verilator accept (see
`lrm-compliance-notes.md`).

### Batch 17: operand signedness, port data types, parameter values

These were wrong values from Verilator `$stop` triage. Each was checked
against xsim. Fixture: `sv_operand_signedness.sv`.

- An operation is signed only when all of its context-determined operands
  are, and those operands extend by the operation's signedness (11.8.1,
  11.8.2). Before, operands extended by their own signedness, so
  `u8 + s4` sign-extended `s4`. The flag also reaches nested operators,
  shifts, unary operators and comparisons. The base of `**` keeps its own
  signedness, because the exponent is self-determined.
- A port read through its connected actual keeps the port's declared
  signedness, including a `wire signed` redeclaration of a non-ANSI port.
- `int i; output i;`: a port direction declared after the data declaration
  forms one port of that type (23.2.2.1). Before, it formed a 1-bit port plus
  a separate signal, which also broke positional connections.
- A value parameter with an explicit type or range is no longer an alias of
  a name default or actual: `parameter [3:0] P = Q` has width 4 (6.20.2).
- A leftmost x or z digit fills the remaining bits of an elaboration-time
  based literal (`2'b?` is `zz`, 5.7.1). Untyped parameters with x/z values
  keep their literal width.
- `$bits` of a struct member is the member's width.
- An unassigned 4-state function result is x, both at run time and in
  constant evaluation (13.4.1, Table 6-7).
- `module automatic` / `package automatic` make tasks and functions
  automatic by default (6.21).
- A multi-character string literal in a packed context is eight bits per
  character (5.9).
- `$dist_uniform` with `start >= end` returns `start` without reading the
  seed, even when the seed is x (Annex N).
- Runner: a Verilator test whose compile fails only under Verilator
  (`fails=test.vlt_all`) may end at its unconditional `$stop`.

Triage of the remaining Verilator stops (2-state expectations, scheduling
races, Verilator-only flags) is in `lrm-compliance-notes.md`.

Corpus effect (interpreter, no case newly fails): sv-tests +8 (1098/1497),
ivtest +33 (1339/2801), Verilator +32 over runs 21 and 22 (685/2119).

### Batch 16: $stop reporting and Verilator driver fidelity

- `fsim simulate` prints `$stop paused the simulation` after the status line
  when a run ends at `$stop` rather than `$finish`. `$stop` suspends a
  simulation (IEEE 1800-2017 20.2), and before this the two endings were
  indistinguishable in batch output.
- Runner: a Verilator test passes when its run neither stops at `$stop` nor
  reports `%Error`. `*-* All Finished *-*` is required only when the test
  calls `test.execute(check_finished=True)`, as in `driver.py`. Before,
  every executing test needed the marker. That failed tests that finish
  without printing it, while a `$stop` after the marker went unnoticed.

Corpus effect: Verilator 29.4% to 31.4% (43 newly pass, none newly fail).

### Batch 15: extension by source signedness, cast widths, enum literals

These were wrong values, not unsupported constructs; Verilator checksum tests
stopped on them.

- A value extends to its target's width by its own signedness, which its
  operands determine (IEEE 1800-2017 11.8.2). Before, assignments and
  task/function arguments sign-extended by the target's signedness:
  `s = 3'b111` into a signed byte gave `8'hff` instead of `8'h07`, and
  `u = 3'sb111` into an unsigned byte gave `8'h07` instead of `8'hff`.
- Unary `-`, `~` and `+` widen their operand to the context width before
  applying (11.6.1). `-8'sh80` into a 16-bit target is `16'h0080`. Before,
  this was right only because extension had wrongly followed the unsigned
  target.
- A size or type cast evaluates its operand in the cast's width (6.24.1):
  `byte_t'(one << 2)` is `8'b100`.
- A constant `if` condition whose arithmetic uses sized literals is no
  longer folded by the host-integer constant evaluator.
  `(4'd15 + 4'd1) != 4'd0` is false. The runtime path applies Verilog
  widths.
- An enumeration literal has its enumeration's width even when its value is
  an unsized integer (`P = 0` in `enum logic [2:0]` is 3 bits, 6.19). HIR
  normalization no longer folds enumeration-literal references into untyped
  literals.
- `FSIM-ELAB-SVTYPE-004` now also rejects a compound assignment or
  increment of an enumeration variable (6.19.4).

Corpus effect (against batch 14): Verilator +5, ivtest +23, sv-tests +4,
no regressions.

Fixture: `sv_expression_widths.sv`, checked against xsim.

### Batch 14: multidimensional packed arrays, parameter typing, %-d

- Selections of multidimensional packed arrays (`logic [3:0][7:0] w`) were
  wrong. `w[2]`, `w[2:1]`, `w[3-:2]` and `w[i]` selected bits of the
  flattened vector instead of whole elements, so reads returned wrong values
  and dynamic writes failed to lower. The HIR now keeps every packed
  dimension (`sv::TypeReference::packed_dimensions`). An element select has
  the width of the remaining dimensions, and outer-dimension selects are
  scaled by the element width (IEEE 1800-2017 7.4.5).
  - Covered: constant and dynamic indices; nested selects (`w[1][3:0]`);
    blocking, nonblocking and continuous writes.
  - A dynamic element read with an unknown or out-of-range index yields X.
  - Several Verilator CRC/checksum tests had stopped on mismatches caused by
    this.
- `%-d` (left-justify flag without a width) prints the minimal decimal, as
  other simulators do. It had been rejected as an invalid format.
- Runner: a Verilator test that never calls `test.execute()` is compiled
  and elaborated but not simulated, as `driver.py` does. Requiring
  `*-* All Finished *-*` from such tests was a runner artifact.

- Parameter typing (IEEE 1800-2017 6.20.2):
  - A value parameter without a type or range takes the width and
    signedness of its value (`parameter Q = 3'd5` is 3-bit unsigned, not a
    signed 32-bit integer). `$bits` agrees.
  - A local parameter with a type or range keeps it. HIR normalization had
    folded `localparam [7:0] L = 300` into the unsized literal 300. Such
    references now stay names, and the lowerer converts the value (44).
    `int` and `integer` local parameters whose value fits still fold.
  - Dynamic element writes use the part-select write operations, which the
    LLVM engine requires for writes wider than one bit.
  - An unsized based literal (`'b1`) is 32 bits wide; an unbased unsized
    literal (`'1`) is one bit when self-determined.
- Assignment-pattern member values convert to the member width (10.9.2).
- A packed structure or union may be compared for equality with an untyped
  integral value (7.2.1). `FSIM-ELAB-SVTYPE-005` had rejected
  `s != 6'b110011`.

Corpus effect: Verilator 26.5% to 29.2% (58 newly pass, none newly fail);
ivtest 13 newly pass; sv-tests unchanged.

Fixtures: `sv_packed_multidim.sv` and `sv_parameter_types.sv`, both checked
against xsim.

### Batch 13: formal subelements, aggregate slices, module items

- Individual association of formal subelements in port maps (IEEE 1076-1993
  4.3.2.2, 1076-2008 6.5.7.1): `d(7) => x, d(6 downto 0) => y` and
  `p.field => z`. The parser combines the parts of one formal into a single
  association. Its actual is an aggregate with the subelements as choices,
  which is accepted before VHDL-2008 when every part is a name. Input ports
  only so far.
- An aggregate range choice whose value is an array supplies a slice
  (`(7 => x, 6 downto 0 => y)`, IEEE 1076-2008 9.3.3.3).
- SystemVerilog modules accept `const` variables (6.20.6), `var`
  declarations with an implicit logic type (6.8), and empty `;` items
  (A.1.10).
- New check `FSIM-SV-SEM-395`: a Verilog (not SystemVerilog) ANSI task or
  function port declaration must name its direction. ivtest br1027c and
  br1027e expect this rejection in Verilog mode. Batch 12 had made them
  pass by accident.

Corpus effect against run 6: VESTs, nvc and VHDL-Compliance gain 8 cases;
ivtest gains 2. Of the 120 `FSIM-VHDL-PARSE-042` cases, most now stop at
other gaps. The largest is a conversion function on the formal side
(`to_x(formal) => actual`, 36 VESTs cases), which the parser cannot yet
tell apart from an indexed formal.

- Runner: VESTs `OUTPUT=` files are no longer compared with the provided
  iofiles. GHDL's own harness (`testsuite.sh`) records them but never
  compares them. The iofiles are inputs for the read tests, and a typed
  file's representation is implementation defined. 53 run-6 "output file
  differs" failures were artifacts of this stricter check.

Fixtures: `vhdl_formal_subelements.vhd` and `sv_module_items.sv`, both
checked against xsim.

### Corpus run 6 (after batches 10 to 12, interpreter)

| Suite | Cases | Pass | Fail | Timeout | Skip | Pass rate |
|---|---:|---:|---:|---:|---:|---:|
| sv-tests | 1497 | 1087 | 409 | 1 | 0 | 72.6% |
| verilator | 2415 | 562 | 1554 | 3 | 296 | 26.5% |
| ivtest | 2826 | 1268 | 1529 | 4 | 25 | 45.3% |
| vests | 3665 | 1974 | 1689 | 2 | 0 | 53.9% |
| nvc | 1482 | 451 | 813 | 3 | 215 | 35.6% |
| vhdl-compliance | 72 | 16 | 56 | 0 | 0 | 22.2% |

Compared with run 5, 177 cases newly pass and 4 newly fail. All 4 are
negative ivtest cases that newly supported syntax now reaches:
- br1027c and br1027e: fixed in batch 13.
- enum_base_fail_string2: an enum base that names a string typedef.
- sv_array_cassign_fail10: an enum array assigned to an integer array.

The last two need elaboration-time type checks (queued).

### Batch 12: display arguments, decimal field widths, shared file handles

- Display and write tasks accept empty arguments, which print a space
  (`$display(a,,b)`). A string literal after the first argument is a format
  for the arguments that follow (`$monitor($time,, "A=%b", a)`). IEEE
  1364-2005 17.1.1 / 1800-2017 21.2.1.
- A decimal value with no explicit field width (`%d`, or a default-format
  argument) is padded to the width of its largest magnitude (IEEE 1800-2017
  21.2.1.3). Examples: `logic [3:0]` prints ` 5`, `integer` prints
  `         42`, `time` prints 20 characters. This applies to `$display`,
  `$monitor`, `$strobe`, `$fdisplay`, `$sformatf` and constant arguments.
  `%0d` still prints the minimal form. Before, fsim never padded, so its
  output differed from every other simulator's. Two application tests had
  encoded the unpadded form and were updated. VHDL images are unchanged.
- A file descriptor opened by one process may be used and closed by
  another, and `$fflush()` flushes every open file (IEEE 1800-2017 21.3).
  Before, using another process's descriptor was a runtime error.

- `typedef string T[...]` (string-element containers, IEEE 1800-2017 6.18)
  and `num()` of an associative array (7.9.1).

Fixtures: `sv_display_arguments.sv` and `sv_string_containers.sv`, both
checked against xsim.

### Batch 11: package signals, declarative use clauses, generate configurations

- Package signals (IEEE 1076-2008 4.7, 6.4.2.3). Each is one design-wide
  object, bound like a package shared variable. Instances write it and read
  it, and processes can be sensitive to it.
- Use clauses in package, entity and architecture declarative regions
  (12.4). They are treated as part of the unit's context clause. Signal
  declarations in an entity declarative region (3.2.3).
- Block configurations of a for-generate may select a static discrete range
  (`for g(0 to 3)`, 3.4.2). Generate specifications may use package
  constants and `'LOW`/`'HIGH`/`'LEFT`/`'RIGHT` of scalar subtypes.
- A component declaration in a block hides the architecture's homograph.
  Directly visible components hide use-visible ones (12.3). Before, both
  were treated as overloads, and the instance matched neither.
- SEVERITY_LEVEL, FILE_OPEN_KIND and FILE_OPEN_STATUS objects: signals,
  ports, array elements, `'POS`, `'VAL`, `'IMAGE`, and initializers using
  their literals. `CHARACTER'VAL` also works now.
- A CHARACTER constant initialized with a logic-looking literal
  (`constant c : character := '0'`) has value 48. Batch 10 had made
  comparisons with `'0'` CHARACTER-typed without changing this.
- New legality checks exposed by the new syntax:
  - `FSIM-VHDL-SEM-115`: an entity statement part contains a non-passive
    statement.
  - `FSIM-FE-VHORDER-004` now also covers `use lib.unit;` naming something
    other than a primary unit.

Corpus effect (full VESTs, nvc and VHDL-Compliance suites): compared with
run 5, 101 cases newly pass and none newly fail. VESTs is at 53.7%.

Fixtures: `vhdl_package_signals.vhd` and `vhdl_generate_configuration.vhd`,
both checked against xsim.

### Batch 10: CHARACTER values, null concatenation, net data types

- CHARACTER values (IEEE 1076-2008 16.3):
  - graphic literals (`'A'`);
  - the non-graphic names `NUL` to `USP`, `DEL` and `C128` to `C159`;
  - logic-looking literals (`'0'`, `'X'`) whose context is CHARACTER: an
    assignment target, an initialized object, a comparison operand, a case
    selector, or a `CHARACTER'` attribute prefix. Without such a context they
    stay BIT or STD_ULOGIC values, so numeric_std `u + '1'` is unchanged.
  - CHARACTER as an attribute prefix (`'POS`, `'HIGH`, ...) and as a case
    selector.
- A null array operand of `&` contributes no elements (IEEE 1076-2008
  9.2.5). This affects 27 VESTs concatenation tests.
- SystemVerilog nets with an explicit 4-state data type (`wire logic [7:0]`,
  `wire integer`, IEEE 1800-2017 6.7.1), and the `vectored`/`scalared`
  keywords.
- New diagnostic `FSIM-SV-SEM-394`: a void function's `return` supplies a
  value (IEEE 1800-2017 13.4.1). Batch 8 had made the sv-tests negative case
  13.4.1--function-void-return pass by accident.
- The corpus runner's Verilator shell now follows the C++ main that Verilator's
  `driver.py` generates: `clk` toggles every 5 units after time 10 (109
  rising edges by time 1100), and only `clk` and `fastclk` are connected. The
  tests count cycles against that main. Before, the shell copied the
  driver's Verilog shell, which gives about 91 edges, so tests that wait for
  cycle 99 never finished. Its input scan also misread
  `input logic [95:0] i` as a port named `logic`.

Fixtures: `vhdl_character_type.vhd` and `sv_net_data_types.sv`, both checked
against xsim.

### Corpus run 5 (after batches 6 to 9 and the Verilator shell fix, interpreter)

| Suite | Cases | Pass | Fail | Timeout | Skip | Pass rate |
|---|---:|---:|---:|---:|---:|---:|
| sv-tests | 1497 | 1066 | 430 | 1 | 0 | 71.2% |
| verilator | 2415 | 549 | 1567 | 3 | 296 | 25.9% |
| ivtest | 2826 | 1238 | 1559 | 4 | 25 | 44.2% |
| vests | 3665 | 1873 | 1790 | 2 | 0 | 51.1% |
| nvc | 1482 | 443 | 821 | 3 | 215 | 35.0% |
| vhdl-compliance | 72 | 16 | 56 | 0 | 0 | 22.2% |

VUnit test cases: 12 of 154 pass. Compared with run 4, 192 cases newly pass.
One newly fails: the sv-tests void-return negative case, fixed in batch 10.

### Batch 9: SystemVerilog class statements and string replication

- A class function method called as a statement (`c.set(5);`) or discarded
  with `void'()` lowers as a method call whose result is discarded. Void
  methods included. This was the most common failing statement in the
  Verilator corpus.
- A static class property reached through an object handle (`obj.count`)
  denotes the class's single storage (IEEE 1800-2017 8.9).
- String replication (`{3{"ab"}}`, IEEE 1800-2017 11.4.12.2).

Fixture: `sv_class_statements.sv`, checked against xsim.

### Batch 8: SystemVerilog function statements and procedural writers

- A void function called as a statement (`bump(2);`) and a discarded
  function result (`void'(f(x));`) failed to lower (IEEE 1800-2017 13.4.1).
  This affected about 100 corpus statements.
- A variable written by procedural statements in several processes, such as
  two `initial` blocks, was rejected with `FSIM-ELAB-DRV-001`. IEEE
  1800-2017 6.5 allows this; the last write determines the value.
  `FSIM-ELAB-DRV-001` now applies only to continuous assignments that
  conflict with other drivers, and to unresolved VHDL signals with several
  processes.

Fixture: `sv_function_statements.sv`, checked against xsim. A targeted rerun
of the run-4 `FSIM-ELAB-HIR-001` cases: 13 more pass. Most of these tests
also use other unsupported constructs.

### Batch 7: STRING generics, top-level overrides, VUnit stand-in

- STRING generics, with or without a default, bound by a generic map or a
  top-level override. Includes `'LENGTH` and the other array attributes of
  a STRING constant or generic, taken from its static value.
- `fsim elaborate --generic NAME=VALUE` overrides root generics: STRING,
  integer or BOOLEAN values, applied to every root that declares the generic.
  This is the corpus runner's hook for VUnit `runner_cfg` and nvc generics.
  New diagnostic: `FSIM-ELAB-GENERIC-009`.
- A protected type body may declare private subprograms that its
  declaration does not list (IEEE 1076-2008 5.6.3). `FSIM-ELAB-VHPROTECTED-003`
  and `-005` are retired.
- Procedure overload resolution distinguishes array types:
  `STD_(U)LOGIC_VECTOR`, `UNSIGNED`, `SIGNED`, `BIT_VECTOR` and user array
  types. Before, any two vectors of equal width matched every overload, and
  the call was reported as ambiguous.
- The VUnit stand-in (`scripts/lrm_corpus/vunit_shim.vhd`) keeps its state in
  a package shared variable and selects test cases by call order, without
  storing names. It reports failures without a test name; the runner
  attributes each failure to the most recently started test case.

VHDL-Compliance: 16 of 72 testbench files and 12 of 154 VUnit test cases
pass. Before this batch, none did.

Fixture: `vhdl_string_generics.vhd`, which uses the harness's new
`generic=NAME=VALUE` header key. It was checked against xsim (`xelab
-generic_top`).

### Corpus run 4 (after batches 4 and 5, interpreter)

| Suite | Cases | Pass | Fail | Timeout | Skip | Pass rate |
|---|---:|---:|---:|---:|---:|---:|
| sv-tests | 1497 | 1037 | 459 | 1 | 0 | 69.3% |
| verilator | 2415 | 435 | 1681 | 3 | 296 | 20.5% |
| ivtest | 2826 | 1229 | 1568 | 4 | 25 | 43.9% |
| vests | 3665 | 1854 | 1809 | 2 | 0 | 50.6% |
| nvc | 1482 | 439 | 825 | 3 | 215 | 34.6% |
| vhdl-compliance | 72 | 0 | 72 | 0 | 0 | 0.0% |

Compared with run 3, 245 cases newly pass and 53 newly fail:

- 50 are negative tests that had been rejected only by an unsupported form.
- 3 previously passed only because `$unit` typedefs were skipped:
  - a wildcard associative-array index (`[*]`);
  - a typedef adding packed dimensions to a user type;
  - a non-ANSI port whose type is declared before its direction.

UVM-based sv-tests now compile the whole UVM library before reaching
randomization, which makes the sv-tests suite much slower.

### Batch 6: package shared variables, typed files

- Shared variables declared in packages and package bodies (protected types,
  IEEE 1076-2008 4.7) are design-global objects. Each is materialized once
  and visible to every design unit through the package's subprograms. This
  unblocks the VUnit stand-in, whose state is a package shared variable.
- Direct VHDL file I/O handles files of integer and physical types of up to
  64 bits, and of two-state types: enumerations, BIT, BOOLEAN, REAL, and
  composites of those. Before, only integer files of up to 32 bits worked
  (86 VESTs cases failed with `FSIM-ELAB-VHFILE-011`).
  - An aggregate actual of `write` takes the file element type as context.
  - `endfile` is true after the last element is read.
- SystemVerilog package parameters whose default is a literal with X or Z
  digits (`parameter int foo = 1'bx;`) no longer fail with
  `FSIM-ELAB-SVPKG-006`.

Fixtures: `vhdl_package_shared_variables.vhd`, `vhdl_typed_files.vhd`. Both
were checked against xsim.

### Corpus run 3 (after batches 2 and 3, interpreter)

| Suite | Cases | Pass | Fail | Timeout | Skip | Pass rate |
|---|---:|---:|---:|---:|---:|---:|
| sv-tests | 1497 | 989 | 503 | 5 | 0 | 66.1% |
| verilator | 2415 | 426 | 1686 | 7 | 296 | 20.1% |
| ivtest | 2826 | 1211 | 1586 | 4 | 25 | 43.2% |
| vests | 3665 | 1745 | 1918 | 2 | 0 | 47.6% |
| nvc | 1482 | 424 | 840 | 3 | 215 | 33.5% |
| vhdl-compliance | 72 | 0 | 72 | 0 | 0 | 0.0% |

Compared with run 2, 93 cases newly pass. Three negative cases newly pass
analysis. They had been rejected only by the retired output-port-default
error, and each exposes an existing gap:

- VESTs tc112: reading a port of mode `out` before VHDL-2008.
- nvc issue885: the default of an output port as the driver's initial
  value.
- nvc force3: the length check on a forced value.

## Known gaps and historical leads

This queue includes historical routing leads. Recheck each case against the
latest batch checkpoint before treating it as an open gap or estimating impact.

Out of scope (owner, 2026-10-11): extensions and behaviour of other
simulators that the LRMs do not define, such as Verilator `$c`/`$cpure`,
Verilator scheduling assumptions (an `always` block without timing seeing
settled nets), Icarus-specific same-time races, and implementation-defined
widths such as 32-bit truncation of unsized literals. Cases that depend on
them are not counted as gaps.

| Gap | Evidence |
|---|---|
| SV: multidimensional interface instance arrays (`ifc a[2][3] ()`); one-dimensional arrays and port arrays done in batch 23 | 14 Verilator cases (`FSIM-SV-PARSE-008`) |
| SV: parameterized class-scoped types (`C#(int)::T v;`) | ~14 Verilator cases (`FSIM-SV-PARSE-001`) |
| SV: the `process` class (`process::self()`, `status`, `kill`, `await`) | 11 Verilator/sv-tests cases |
| SV: modport expressions (`modport mp(input .p(expr))`) | 9 Verilator cases |
| SV: static task/function locals shared across processes and reachable hierarchically (`task.var`) | ivtest `ldelay1`, `pr307a`, `br1004` |
| SV: `force` with a non-constant expression follows the expression's changes | ivtest `pr245` and others |
| SV: hierarchical task and function calls (`u.f(x)`, `block.f()`, `m1.c.f()`): callable bodies are lowered in the caller's instance context | ~25 ivtest/Verilator cases (`unsupported expression kind 9 '.f'`) |
| SV: `%v` strength format; needs a per-bit resolved-strength query from the display formatter (the interpreter resolves strength per signal) | 18 ivtest cases (`FSIM-SV-SEM-042`) |
| VHDL: interface functions with an `is <>` default named by an operator symbol (`function "+" (a, b : t) return t is <>`) must resolve to the predefined operator of the actual type; the syntax parses since batch 39 | nvc gentype8, gentype9, conf5 |
| VHDL: protected function bodies other than a single `return` statement (they are inlined as expressions) | VUnit-style testbenches |
| VHDL: the initial effective value of a port whose actual is a conversion function call is the converted initial value of the actual (IEEE 1076-2008 14.7.3); fsim starts from the formal's default until the conversion runs | VESTs tc3070 family |
| SV: members of a packed structure that is itself a member of an unpacked structure (`u.packed_member.field`), read and written | Verilator `t_param_type5` and others |
| SV: `disable` of a task or of a named block from another process | 12 ivtest, 9 Verilator cases (`FSIM-ELAB-SVDISABLE-001`) |
| VHDL: record element constraints (`rec(y(1 to 3))`) | 26 nvc cases (`FSIM-VHDL-PARSE-009`) |
| VHDL: VHDL-2008 element constraints of arrays and records (`t(1 to 2)(7 downto 0)`, `t(open)(7 downto 0)`, `rec(f(1 to 3))`); needs element constraints in the HIR subtype and in every array and record layout | about 50 nvc cases (`FSIM-VHDL-PARSE-009`, `-018`, `-135`) |
| VHDL: individual association in procedure calls with a conversion function in the formal part (`f(b) => x`) or a signal-class formal; plain subelement association done in batch 38 | VESTs tc146, tc154; nvc signal25, signal27, signal30 |
| VHDL: external names (`<<signal .a.b : t>>`) | 9 nvc cases |
| VHDL: broader predefined-vector qualification and formal coverage beyond the scoped INTEGER_VECTOR paths closed in batch 57 | Recheck REAL_VECTOR cases against the latest baseline; nvc agg7 is excluded for its direction assumption |
| SV: nonblocking whole-object writes of unpacked aggregates (`out <= buffer[i]`) and continuous assignment to unpacked structure members | Verilator |
| SV: `randomize` and constraint blocks | 36 sv-tests cases (`FSIM-SV-CLASS-005`) |
| VHDL: chained signal attributes (`clk'delayed'last_event`) | 16 VESTs cases (`FSIM-VHDL-PARSE-046`) |
| SV: UVM `run_test` from source. User components now specialize and construct (batch 38), but `run_test()` inlines the library body, which reaches `uvm_coreservice_t::get()` and `uvm_init`; the class executor cannot run package functions, timing or a suspendable run phase. The native phase runner (`execute_uvm_function_phase`, `execute_uvm_task_phase`, `SystemVerilogUvmTestRunnerService`) is only driven by unit tests: it needs a production phase loop, a `uvm_phase` object for objections, and a suspendable run phase | ~100 UVM sv-tests (the UVM cases that pass are elaboration checks) |
| VHDL: resolution of guarded `bus`/`register` signals (disconnected drivers leave the driving-value array, so the resolver needs one variant per driver count) and of arrays of resolved elements; general resolution functions done in batch 31 | 7 VESTs (`FSIM-ELAB-VHDLGUARD-001/-002`), nvc guard4, VESTs tc1709/tc1711 |
| VHDL: record signals initialized from static record constants that contain 2-D arrays | VESTs tc440, tc449, tc473, tc474, tc485, tc486 |
| VHDL: names in package-body subprograms are not checked at analysis (the validator selects only entity and architecture scopes) | VESTs tc907-tc909 |
| VHDL: constant names folded during normalization lose their type, so slice bounds given by constants are not checked against the index type | VESTs tc1125, tc1133 |
| SV: run-time rise/fall delays on a continuous assignment to a bit or part select (`assign #(d1, d2) o[1] = i`) | 5 ivtest `rise_fall_*` cases |
| SV: associative-array assignment patterns with a `default:` member, and `foreach` over a `bit [63:0]`-indexed associative array | Verilator `t_assoc_wildcard*` |
| SV: packed arrays of packed structures (`entry_t [3:0] e;`, `struct packed {...} [15:0]`) | 6 Verilator cases (`FSIM-SV-PARSE-008`) |
| SV: interface parameters read through a port (`a.PARAM`) | Verilator interface cases |
| SV hierarchical references (`s.a`) in expressions | ~361 Verilator cases (`FSIM-ELAB-HIR-001` kind 3) |
| Verilator: parameter defaults, typedefs, class out-of-block methods, `$sformatf` formats | top Verilator causes |
| sv-tests: SV class/randomization constraint syntax | `FSIM-SV-UNSUPPORTED-001` (73) |
| VHDL: `wait for` with a non-static duration (for example a TIME formal) | testbench procedures |
| VHDL: `rising_edge`/`falling_edge` of a signal-class formal | testbench procedures |
| VHDL: STD_ULOGIC index values given as character literals (`t('H')`, `array (std_ulogic) of ...` aggregates); CHARACTER done in batch 10, STRING element writes in batch 38 | IEEE package tables, VESTs |
| VHDL: invalid input accepted (for example integer literals as `unsigned` aggregate elements) | sv-tests/VESTs negative cases |
| VHDL: output-port defaults as the driver's initial value | VESTs, nvc issue885 |
| VHDL: reading an `out` port is accepted before VHDL-2008 | VESTs tc112 |
| VHDL: individual association of formal subelements (`rec.field => x`) and formal conversion functions (`to_x(F) => S`) | 98 VESTs/nvc cases (`FSIM-VHDL-PARSE-042`) |
| VHDL: nested package declarations (`package inner is ... end package;` inside a package); design-unit instantiation of same-file templates done in batch 40 | nvc genpack13, issue653, issue732 |
| VHDL: names in a configuration are not resolved against the configured architecture (architecture-local constants in generate specifications); design-wide unique names are used as a fallback | nvc config cases (`FSIM-ELAB-VHCONFIG-010`) |
| VHDL: aggregate initializers of arrays of SEVERITY_LEVEL (`(others => note)`) | VESTs |
| VHDL: element constraints of ports taken from the actual, `'element`, and element constraints in protected types (syntax and layouts done in batch 33, formals in batch 34) | ~35 nvc cases |
| VHDL: `'subtype` of an object as a subtype indication (VHDL-2008) | 14 nvc cases |
| VHDL: a `for` loop over `'RANGE` of a runtime-sized STRING, and element writes to it | 5 VESTs cases (`FSIM-ELAB-071`) |
| VHDL: signal initializers that do not fold at elaboration (function calls, composite constants of records and nested arrays, arrays of CHARACTER other than STRING filled from named constants). Most of the VESTs cases are static values the elaboration evaluators do not cover; a run-time initializer would need a SimIR deposit operation that runs before the first process | ~21 VESTs/nvc cases (`FSIM-ELAB-HIR-001` "not a bounded integral value") |
| SV: pure virtual tasks called through a base handle, and string-returning class methods called from inside another class method (string results elsewhere done in batch 38) | Verilator/sv-tests class cases |
| SV: a class declared in `$unit` uses the caller's time unit for `$time` and `$printtimescale` (packages and `$unit` subroutines done in batch 38) | ivtest br1003a |
| SV: a format held in a string variable (`$display(fmt, ...)`, `$sformatf(fmt, ...)`, `$sformat`): formatting needs a run-time format parser over lowered arguments (21.2.1, 21.3.3) | ~8 ivtest/Verilator cases (`FSIM-ELAB-SVSTRING-019`) |
| SV: constrained randomization: relations between wide variables, bit-level constraints, `dist` over sampled domains, container properties (interval bounds done in batch 50) | Verilator `have_solver` cases, sv-tests chapter 18 |
| SV: MOS and CMOS switches with an unknown control (L and H outputs), switch and primitive delays on `tranif`, and `$countdrivers` | ivtest `cmos`, `pr1787394*`, `switch_primitives`, `pr2941939`, `countdrivers*` |
| SV: `new` for a variable of a specialized class type (`box #(5) b; b = new;`) constructs the default specialization | testbenches using parameterized classes |
| VHDL: user resolution functions other than a single OR/AND reduction (a runtime resolver calling the function on the driver values) | 28 VESTs cases (`FSIM-ELAB-VHRESOLVE-002`/`-004`), nvc |
| VHDL: indexing a deferred unconstrained constant completed in a package body (`DC(5)` of `constant DC : STRING;`); local and architecture constants done in batch 41 | VESTs tc54 |
| VHDL: library-qualified subprogram calls without a use clause (`work.pkg.f(x)`): the qualified lookup finds the package prototype but the call is not resolved to its body | VESTs dynamic_package_procedure_for_loop, generic package actuals |
| Compiled engine: a procedure that reads a formal whose actual is a literal fails with "register has not been initialized" (the interpreter is correct) | default-engine users; the corpus runs the interpreter |
| VHDL: generic packages with protected types, interface package generics (`package p is new g generic map (<>)`) and templates from other design files | ~15 nvc genpack/issue cases |
| VHDL: records whose elements are two-dimensional or subtype-constrained arrays as signal initial values | 11 VESTs cases (`FSIM-ELAB-HIR-001` initializer) |
| CLI: `fsim elaborate --top <entity>` for a VHDL entity fails with `FSIM-ELAB-004` (the CLI qualifies the name without an architecture); `entity(arch)` works | usability |
| SV: an X/Z or out-of-range dynamic index in a signal write must skip the write instead of failing (interpreter and LLVM paths) | Verilator, ivtest (`FSIM-RUN-0001`) |
| SV: `void'($fgets(...))` and other discarded system-function calls | sv-tests chapter 21 |
| SV: packed arrays of a named type (`T [3:0] v;`, `typedef T1 [7:0] T2;`) | ~80 ivtest/Verilator cases (`FSIM-SV-UNSUPPORTED-004`/`-024`) |
| SV: `%t` of integer expressions and time-array elements (scaled from the module unit to `$timeformat`), and `$monitor` `%t` arguments other than `$time` | ~9 ivtest cases |
| SV: a genvar hides a later module-level declaration of the same name | ivtest `genvar_compressed`, `genvar_inc_dec`, `pr2109179` |
| VHDL: conversion functions on formals of directly instantiated entities, and of inout formals with only a formal conversion (component output and inout formals done in batch 25) | VESTs (`FSIM-ELAB-VHCOMP-009`) |
| VHDL: individual association of output-port subelements | VESTs, nvc |
| SV legality: an enum base that names a non-integral typedef; unpacked array assignment between enum and integer element types | ivtest negative cases |
| VHDL: an array element such as `bit_vector(0 to N-1)`, constrained with non-literal bounds, was treated as unconstrained and gated to VHDL-2008 (fixed in batch 4) | 126 nvc/VESTs cases (`FSIM-FE-VHSTD-003`) |
| VHDL: unconstrained array ports (`port (d : in bit_vector)`) bind with width 1 (`FSIM-ELAB-BIND-020`) | VESTs, generic-width library cells |
| VHDL: analysis-time legality still unchecked: expanded names, overloaded enumeration literals, type conversions between distinct floating types declared as `range` (kept as REAL subtypes), attributes of formal signal parameters, index count and type of indexed names, package body conformance | about 350 VESTs negative cases (`accepted invalid input`) |
| SV: package-level events (`event e;` in a package or `$unit`) | Verilator fork/process tests |
| SV: property writes in methods of classes declared inside a module fail at run time ("class HIR assignment target is not executable", "SimIR driver assignment width mismatch"); classes at `$unit` or in packages work | sv-tests chapter 8 |
| SV: string-valued (virtual) class methods in expressions such as `$display` | Verilator, sv-tests |
| SV: wildcard associative-array indices (`[*]`) | sv-tests typedef cases |
| SV: typedef adding packed dimensions to a user type (`typedef T1 [7:0] T2;`) | ivtest |
| SV: non-ANSI port type declared before its direction (`T x; output x;`) | ivtest module_nonansi_* |
| VHDL: function overload resolution by array type (procedures done in batch 7) | VUnit `check_equal`-style helpers |
| SV: randomization and constraint blocks (`randomize`, `constraint`, `with`) | UVM-based sv-tests, chapter 18 |
| SV: `fork` inside functions and tasks (lowering) | sv-tests, UVM |
| SV: hierarchical references in event controls, continuous assignments, generate processes | Verilator |
| VHDL: VHDL-2008 STD.ENV package (`use std.env.all`) | VHDL-2008 testbenches |
| VHDL: TIME `'image` and `TIME'HIGH`; predefined TIME unit names as primaries (`us = 1000 ns`; user physical unit names done in batch 39) | VESTs tc298, tc299 |
