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
`.local-artifacts/lrm-corpora/uvm`.

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

## Known gaps queue (ranked by blocked corpus cases; refreshed per run)

| Gap | Evidence |
|---|---|
| SV: interface array ports (`a_if.mp p [N]`) and their element bindings | 35 Verilator cases (`FSIM-SV-PARSE-002`) |
| SV: UVM `run_test` from source: the phase, objection and report services exist but are only driven through the C++ API | 100 UVM sv-tests |
| SV: interface parameters read through a port (`a.PARAM`) | Verilator interface cases |
| SV hierarchical references (`s.a`) in expressions | ~361 Verilator cases (`FSIM-ELAB-HIR-001` kind 3) |
| Verilator: parameter defaults, typedefs, class out-of-block methods, `$sformatf` formats | top Verilator causes |
| sv-tests: SV class/randomization constraint syntax | `FSIM-SV-UNSUPPORTED-001` (73) |
| VHDL: `wait for` with a non-static duration (for example a TIME formal) | testbench procedures |
| VHDL: `rising_edge`/`falling_edge` of a signal-class formal | testbench procedures |
| VHDL: STD_ULOGIC index values given as character literals (`t('H')`, `array (std_ulogic) of ...` aggregates); CHARACTER done in batch 10 except STRING element writes (`s(1) := 'A'`) | IEEE package tables, VESTs |
| VHDL: invalid input accepted (for example integer literals as `unsigned` aggregate elements) | sv-tests/VESTs negative cases |
| VHDL: output-port defaults as the driver's initial value | VESTs, nvc issue885 |
| VHDL: reading an `out` port is accepted before VHDL-2008 | VESTs tc112 |
| VHDL: individual association of formal subelements (`rec.field => x`) and formal conversion functions (`to_x(F) => S`) | 98 VESTs/nvc cases (`FSIM-VHDL-PARSE-042`) |
| VHDL: library-level package instantiation (`package p is new work.g generic map (...)`) | 21 nvc/VHDL-Compliance cases (`FSIM-VHDL-UNSUPPORTED-022` 'new') |
| VHDL: names in a configuration are not resolved against the configured architecture (architecture-local constants in generate specifications); design-wide unique names are used as a fallback | nvc config cases (`FSIM-ELAB-VHCONFIG-010`) |
| VHDL: aggregate initializers of arrays of SEVERITY_LEVEL (`(others => note)`) | VESTs |
| VHDL: typed files of non-integer elements (`file of real`, records, arrays, TIME), read in the VESTs text format (one value image per line; composites space-separated) | 111 VESTs/nvc cases (`FSIM-ELAB-VHFILE-011`) plus 20 VESTs reads |
| SV: an X/Z or out-of-range dynamic index in a signal write must skip the write instead of failing (interpreter and LLVM paths) | Verilator, ivtest (`FSIM-RUN-0001`) |
| SV: `void'($fgets(...))` and other discarded system-function calls | sv-tests chapter 21 |
| SV: packed arrays of a named type (`T [3:0] v;`, `typedef T1 [7:0] T2;`) | ~80 ivtest/Verilator cases (`FSIM-SV-UNSUPPORTED-004`/`-024`) |
| VHDL: conversion functions on port association formals (`to_x(formal) => actual`) and on actuals | 36+ VESTs cases (`FSIM-ELAB-VHCOMP-009`) |
| VHDL: individual association of output-port subelements | VESTs, nvc |
| SV legality: an enum base that names a non-integral typedef; unpacked array assignment between enum and integer element types | ivtest negative cases |
| VHDL: an array element such as `bit_vector(0 to N-1)`, constrained with non-literal bounds, was treated as unconstrained and gated to VHDL-2008 (fixed in batch 4) | 126 nvc/VESTs cases (`FSIM-FE-VHSTD-003`) |
| VHDL: unconstrained array ports (`port (d : in bit_vector)`) bind with width 1 (`FSIM-ELAB-BIND-020`) | VESTs, generic-width library cells |
| VHDL: analysis-time legality (index-constraint bounds and types, slices of multidimensional arrays, labels as primaries) | about 33 VESTs negative tests exposed by batch 4, part of the 724 accepted-invalid cases |
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
| VHDL: TIME `'image` and `TIME'HIGH`, physical unit names as primaries (`us = 1000 ns`), user-defined attributes | VESTs |
