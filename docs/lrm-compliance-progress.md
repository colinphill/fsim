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
| SV hierarchical references (`s.a`) in expressions | ~361 Verilator cases (`FSIM-ELAB-HIR-001` kind 3) |
| Verilator: parameter defaults, typedefs, class out-of-block methods, `$sformatf` formats | top Verilator causes |
| sv-tests: SV class/randomization constraint syntax | `FSIM-SV-UNSUPPORTED-001` (73) |
| VHDL: `wait for` with a non-static duration (for example a TIME formal) | testbench procedures |
| VHDL: `rising_edge`/`falling_edge` of a signal-class formal | testbench procedures |
| VHDL: CHARACTER and STD_ULOGIC index values given as character literals (`t('H')`, `array (std_ulogic) of ...` aggregates), which need character literals resolved by type | IEEE package tables, VESTs |
| VHDL: invalid input accepted (for example integer literals as `unsigned` aggregate elements) | sv-tests/VESTs negative cases |
| VHDL: output-port defaults as the driver's initial value | VESTs, nvc issue885 |
| VHDL: reading an `out` port is accepted before VHDL-2008 | VESTs tc112 |
| VHDL: individual association of formal subelements (`rec.field => x`) and formal conversion functions (`to_x(F) => S`) | 98 VESTs/nvc cases (`FSIM-VHDL-PARSE-042`) |
| VHDL: package-level signals | 138 nvc/VESTs cases (`FSIM-VHDL-UNSUPPORTED-022`) |
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
| VHDL: package-level signals | 14 VESTs/nvc cases |
| VHDL: VHDL-2008 STD.ENV package (`use std.env.all`) | VHDL-2008 testbenches |
| VHDL: CHARACTER objects, TIME `'image`, user-defined attributes | VESTs |
