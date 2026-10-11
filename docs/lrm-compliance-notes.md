<!-- SPDX-License-Identifier: Apache-2.0 -->
# LRM compliance work: design notes

Working notes for closing the language gaps found by the public compliance
corpora (`scripts/lrm_corpus.py`). Progress and results are in
[lrm-compliance-progress.md](lrm-compliance-progress.md).

## VHDL subprograms (map as of 2026-10-08)

- Frontend: `FunctionArgument` (design_core.hpp) has no object class;
  `InterfaceObjectClass {Constant, Variable, File}` has no Signal;
  `ProcedureArgument`, `FunctionDeclaration`, `ProcedureDeclaration`.
  Parameter parsing in `vhdl_parser_functions.cpp`
  (`parse_vhdl_function_parameters`, `parse_vhdl_procedure_parameters`); body
  statement whitelists raise UNSUPPORTED-037 (functions) and -044
  (procedures).
- `parse_vhdl_type` sets `packed_range` only for literal bounds, so
  031/040 reject `slv(7 downto 0)` but not `slv(W-1 downto 0)`; `time`,
  `real` also carry a packed range and are rejected as parameters.
- HIR: `VhdlHirBuilder::add_function/add_procedure`
  (application_vhdl_hir_callables.cpp); formals are `DeclarationForm::port`;
  `vh::ObjectClass` already has `signal`. Statements map in
  application_vhdl_executable_hir.cpp (Assert -> assertion, Report -> report).
- Legality: `validate_vhdl_callable_legality` (vhdl_callable_legality.cpp)
  only for entity/architecture callables; VHLEGAL-006 rejects any procedure
  call from a pure function (stricter than the LRM).
- Lowering (lowerer_hir_callable.cpp): calls are NOT inlined; each callable
  body is emitted once per process per frame key (`HirCallableFrame`), called
  with `CallableFramePush`/`Call`/`Return`/`CallableFramePop`; the runtime has a
  per-process call stack that survives waits. Parameters are packed registers;
  width from `hir_callable_formal_binding`; domains limited by
  `callable_scalar_domain` (Bit2, Logic4, Logic9, Boolean, Integer 32/64).
  Unconstrained array formal ranges come from the actual and are part of the
  function frame key; procedure frames are keyed only by the callable (latent
  bug: one actual width per procedure per process).
- Strings: dynamic `StringRegisterId` registers for VHDL string variables;
  `string` formals work for string-returning functions only; packed-result
  functions and procedures reject string formals.
- report/assert lower in callable bodies unchanged once the parser admits
  them; elaboration-time evaluators (`HirIntegralEvaluator`,
  compiled_design_specialization.cpp) fail on report/assert/procedure call —
  must keep failing (not no-op) for VHDL so runtime reports are not dropped
  by constant folding.
- Signal assignment in procedures: only the parser blocks it; drivers are
  collected from callable bodies. Signal formals need a new binding kind
  (frame keyed by actual SignalId). Hazard: `wait on <local>` lowers to
  WaitForever silently.
- Package objects: scalar and logic-vector package constants fold. Composite
  constants with other elements become read-only design signals on first
  use (`HierarchyBuilder::compiled_vhdl_package_constant_signal`, called
  through `Lowerer::set_package_constant_signal`). Architecture constants of
  that kind are materialized in `materialize_compiled_vhdl_declaration`.
  There are still no package-level signals or shared variables. They need
  design-global objects registered into every instance's signal and
  container maps; the on-demand constant hook is the precedent.
- STD.ENV: `vhdl_simulator_api` gates every std.env name to VHDL-2019
  (vhdl_parser_expressions.cpp, vhdl_parser_statements.cpp); elaboration gate
  FSIM-ELAB-VHENV-001 in lowerer_hir_callable.cpp; std/env.vhdl injected only
  for 2019 units (application_standard_library.cpp).
- Top-level generics: roots elaborate with empty actuals
  (`add_compiled_vhdl_root`, `add_root` in hierarchy_packages.cpp; caller
  elaborate.cpp). Hook: supply `SpecializedHirActualIdentity` actuals.
  UNSUPPORTED-018 (vhdl_parser_core.cpp) rejects string generics; string
  actuals exist only via the SV string-identity encoding.

## SystemVerilog

- `$time`/`$stime`/`$realtime` in a unit without a timescale: lowering now
  records unit/precision 0 = project resolution
  (`systemverilog_time_function`).
- Port actuals of different packed width: bound through the `$actual_`
  adapter instead of aliasing (hierarchy_sv_ports.cpp).

## SystemVerilog hierarchical references (plan, 2026-10-08)

- `s.a` is folded by the parser into one identifier text "s.a"
  (verilog_parser_expressions_part2.cpp `parse_postfix`); non-identifier bases
  become `@sv-select:<member>` chains. HIR `referenced_name` finds nothing;
  `dependencies.hierarchy` is set by compiled_design_normalization.cpp.
- Choke point: `Lowerer::hir_direct_signal` (lowerer_hir_capability.cpp) —
  reads, widths/domains (`hir_direct_signal_binding`), writes, and
  sensitivity all go through it. SV process event lists use
  `hir_referenced_declaration` instead and need a direct-signal check.
- Ordering: `instantiate_compiled_systemverilog_unit` lowers a parent's
  processes before instantiating children, so downward references must be
  lowered after the child worklist (precedent:
  `PendingVirtualInterfaceInitializer`); sibling/`$root` forward references
  need a global queue drained in `HierarchyBuilder::finalize()`.
- Resolve only full paths in `design_.signal_by_name_` (the single-root
  shortcut keys are ambiguous): downward from `hierarchy_`, upward through
  ancestor prefixes / module names (`SpecializationInfo`), `$root.`.
- Template caching hazard: processes using hierarchical references must not
  be remembered or replayed (`replay_systemverilog_process_template`,
  `lower_cached_systemverilog_concurrent_statement`).

## VHDL range constraints from attributes and subtype names (batch 4)

- The parser (`VhdlParser::parse_vhdl_discrete_range`) rewrites `x'range`,
  `x'reverse_range` and a discrete subtype name `T` into `'left`/`'right`
  bound expressions with `direction_from_bounds` set
  (`frontend::DiscreteRangeExpression`, `semantic::vhdl::RangeConstraint`).
- Consumers that evaluate the bounds set `descending = left > right`: the
  resolver's `materialize_boundaries`, the specialization evaluator's
  `concrete_range`, the elaboration `compiled_vhdl_signal_type` helper and the
  lowerer's attribute and selection paths. Sites that only compare the
  declared direction with the bounds skip the comparison for flagged ranges.
- Locals in subprograms fold frame-dependent bounds (`vec'left` of an
  unconstrained formal) with `hir_constant_integer` before resolving their
  subtype, so each call frame sizes them from its actual.

## SystemVerilog statement forms (batches 8 and 9)

- A function called as a statement becomes a `container_method` statement
  whose value is the call: `void'(f())` already used this form (value
  `@sv-cast:void`). The executable HIR builder rewrites module-level calls
  of functions (`SystemVerilogExecutableBuilder`). The frontend class
  resolver (`Resolver::resolve_task_call`) rewrites `obj.f(...)` of a
  function method outside class methods. The lowerer's `container_method`
  case discards user, class and static method calls.
- Inside class methods, `this.f()` statements keep the `@sv-task:` form,
  which the runtime class executor (application_class_hir_execution.cpp)
  runs directly.
- Void functions and methods report a one-bit placeholder result
  (`hir_callable_type`, `hir_class_method_profile`).
- Open issue: property writes in classes declared inside a module fail at
  run time, while `$unit` and package classes work. A task-form call into a
  module-local class reports a SimIR driver width mismatch. This suggests
  the call reaches the method body through module task lowering instead of
  the class executor.

## Verilator expectations outside the LRM (batch 17 triage)

These Verilator test_regress cases stop at `$stop` under fsim. Their
expectations rely on Verilator's 2-state values or on its scheduler. xsim
gives fsim's result in each case checked.

- t_select_plus STOP22: a partly out-of-range `-:` write writes its in-range
  bits (11.5.1). The test expects the write to be dropped. xsim agrees with
  fsim.
- t_sys_system STOP2: `$system` returns the C `system()` status (Annex D,
  2560 for `exit 10`), as xsim does; Verilator returns the exit code.
- t_unconnected STOP1: `` `unconnected_drive`` is applied at the instance
  site. xsim also stops here.
- t_math_eq STOP3, t_opt_inline_varxref_inlineddots STOP1,
  t_scheduling_5 STOP1: these depend on process order within a time step
  (4.7) or on a process retriggering on its own writes. Verilator settles
  combinational logic first.
- t_split_var_4 STOP2, t_opt_split_no STOP5: an x from an uninitialized
  register reaches the checksum or the `if` (xsim agrees: sum is x).
- t_fourstate_no_fourstate: runs with `--no-fourstate`.

## Corpus disagreements resolved by xsim (batch 18)

- An unranged port or classic task argument direction declaration followed
  by a ranged data declaration (`output a; reg [1:0] a;`) takes the data
  declaration's range. xsim and Verilator (t_opt_merge_cond_bug_3409)
  accept it; ivtest's module_nonansi_vec_fail2, task_nonansi_vec_fail3 and
  pr1704013 expect a compile error. fsim follows xsim.
- A different-type enumeration or packed-structure equality is an integral
  comparison (xsim warns); ivtest enum_order relies on it.

## Corpus disagreements resolved by the LRM and xsim (batch 19)

- Verilator `t_clocked_release_combo` expects an `always_comb` variable to
  return to its driven value at `release`. IEEE 1800-2017 10.6.2 keeps a
  released variable's forced value until its next assignment, and the
  `always_comb` block does not run again because its input does not change.
  xsim keeps the forced value too, so the test now stops.
- A variable driven by a continuous assignment re-evaluates at `release`,
  as 10.6.2 requires; xsim keeps the forced value there as well.
- ivtest `nb_ec_real` expects a `repeat(0)` nonblocking update to be
  visible after `#0`. The update matures in the NBA region, after the
  inactive region that `#0` resumes in; xsim agrees with fsim. The test
  also uses the iverilog-only `$simtime`.
- ivtest `real_wire_force_rel` checks a `wire real` immediately after a
  blocking write to its driver, before the continuous assignment can
  update it; xsim rejects `wire real` altogether.

## Corpus disagreements resolved by the LRM and xsim (batch 20)

- ivtest `pv_wr_vec*_nb_ec` read a vector right after `-> e` and expect
  the `x[...] <= @e v` update to be visible. The update matures in the NBA
  region of that time step (10.4.2), after the reading statement.
- ivtest marks bit- and part-select targets of a procedural `assign`
  (`assign bus[0] = ...`) as legal, while 10.6.1 restricts the target to a
  whole variable. fsim keeps accepting packed selects and rejects array
  elements, which ivtest also expects to fail.
- Packed structures of different types are assignment compatible as
  integral values (6.22.3), so fsim no longer rejects passing one where
  another is expected.

## Sequences, rewrites and corpus disagreements (batch 21)

- Concurrent properties outside the specialized slices compile to sequence
  automata advanced by the directive's own process once per clock tick.
  Attempts occupy 64 slots (start tick modulo 64), and every automaton state
  is a 64-bit vector of the slots positioned there, with a count per slot
  for attempts that started together. An antecedent match starts consequent
  attempts in the same tick; `|=>` with a sequence consequent is the same
  automaton with a leading `##1`. Outcomes are counted during the step and
  the action blocks run after it, because reads after the first action are
  no longer sampled. An attempt still running 64 ticks after it started is
  abandoned when its slot is reused. Automata are limited to 192 states.
- The first design gave every attempt a forked thread with block-local
  state. Fork children share their process's frame, so overlapping
  attempts overwrote each other's state: `a ##1 b[*2]` never matched when
  attempts overlapped, and a `b[->1]` consequent failed spuriously. Static
  block variables in an `always` process persist between activations, but
  in a concurrent-assertion process only process variables do, so the
  automaton state is declared there under a per-directive name prefix.
- New attempts start through a one-branch `fork ... join`, so `$assertoff`
  (which filters assertion forks) stops new attempts while running ones
  continue (20.12). `$assertkill` does not yet end running attempts of this
  evaluation.
- A predicate that is one named sequence instance stays with the
  specialized slices, which report attempts still running at the end of
  simulation; the general evaluation does not.
- A second `WaitRegion` to the region a process is already in now continues
  in place; it used to fail the process silently, which dropped any later
  action of the same tick.
- The specialized `cover property` slice reported one match per tick when
  several attempts matched then (`b[*1:3] ##1 c`): its actions in that tick
  ran into the `WaitRegion` failure above. 16.14.3 counts each attempt, as
  it now does.
- A vacuous success runs the pass action, as 16.14.1's default and
  Verilator do; xsim does not run it. `disable iff` reads the sampled
  value of its condition, while 16.12 and xsim use its current value.
- `randcase` and `randsequence` are rewritten into ordinary statements
  before elaboration: weights are summed and compared with `$urandom`,
  productions expand inline in named blocks, production arguments become
  block variables, and `break`/`return` in a code block disable the
  randsequence or production block. `rand join` runs its productions in
  order, which is one interleaving 18.17.5 allows. Recursive productions
  and productions with return values are rejected (`SV-UNSUPPORTED-046`).
- An assignment within an expression (11.3.6) runs as a statement just
  before the statement containing it; its value is the target's new value.
  Inside a loop condition it would run only once, so it is rejected there
  (`SV-UNSUPPORTED-047`).
- sv-tests `18.17.2--if-else-production-statements_{0,2}_fail` declare an
  undeclared `switch` inside a compilation-unit function that nothing
  calls. fsim checks a callable body only when it is elaborated, so these
  now compile; they used to fail only because `randsequence` did not parse.
- ivtest `sv_wildcard_import4` declares a module `event e` after using
  the wildcard-imported package event `e`; 26.3 makes that illegal, but fsim
  does not yet track earlier wildcard references. It used to fail only
  because package events did not parse.

## Rewrites, class execution and corpus disagreements (batch 22)

- `foreach` is rewritten into nested `for` loops before elaboration. Each
  loop runs from `$left` towards `$right` of its dimension using
  `$increment`, so descending and ascending ranges both iterate in
  declaration order. An associative array iterates with `first`/`next`.
- A type named through an interface (`typedef ifc.data_t t;`,
  `localparam type t = ifc.sub.t`) is copied into the referencing unit.
  The interface parameters it depends on become hidden parameters
  `__fsim_ifp_<instance>__<name>`. A local instance supplies its overrides.
  An interface port's parameters are bound at elaboration from the
  connected instance. A typedef of an interface declared inside a generate
  block is not found.
- Class methods run in the host class interpreter. It now handles loops,
  `case`, `break`/`continue`, task calls, `$display`/`$write`, string
  properties, the implicit function result variable, and `new`. `$stop`,
  `$finish` and `$fatal` inside a class method end the method with an
  error instead of pausing the simulation.
- Property initializers that are not literals run in the constructor, after
  the base class's constructor (8.7). A static class-handle property
  initialized by `new` is constructed before any process runs.
- An enumeration literal of a typedef declared in another compilation
  scope is not always linked to its declaration in the compiled HIR. The
  class interpreter takes the value of the unique literal with that name.
- `local` and `protected` member visibility (8.18) is checked
  (`SV-CLASS-028`). The sv-tests encapsulation negatives depend on it, now
  that class bodies execute.
- `unique`, `unique0` and `priority if` take the same branch as a plain
  `if`. Their violation reports (12.4.2) are not issued.
- Immediate `assume` behaves as `assert`. Immediate `cover` runs its pass
  statement when the expression holds; it records no coverage. A deferred
  assertion written as a module item runs in an implicit `always_comb`
  (16.4.3).
- A `global clocking` block is recorded as a clocking block named
  `$global_clock` (or its own name). The `_gclk` sampled functions are not
  yet tied to it; the sv-tests that use them never clock their assertions.
- `==` and `!=` (and wildcard `==?`/`!=?`) are decided by a known differing
  bit even when other bits are X or Z (11.4.5); before, any unknown bit
  gave X. xsim agrees. This applies to the interpreter, the static kernel
  and the LLVM engines.
- A net driven by a delayed continuous assignment starts as X instead of Z
  (iverilog, xsim). The X is the net's initial value, so no value change
  happens at time zero.
- `%d` of a partly unknown value prints `x`/`z` when all bits are X/Z, and
  otherwise `X` if any bit is X, else `Z` (21.2.1.4).
- Runner: ivtest `CO` (compile-only) cases now elaborate and pass without a
  PASSED line, as iverilog's harness treats them. A module with an escaped
  name (`\$I178`) is a valid top and root alias.
- ivtest `pv_wr_vec*_nb_ec` check a nonblocking event-controlled write in
  the same time step as the `-> e` that releases it, expecting the update
  to be visible before the NBA region. That is iverilog scheduling, not the
  LRM's; they still fail.
- `%t` of a value other than `$time` (a `time` variable, a plain vector)
  ignores `$timeformat` and the default width of 20, and fails for a
  vector without time metadata (ivtest `automatic_events3`).

## Time zero, implicit nets and corpus disagreements (batch 23)

- Undeclared identifiers in expressions still become implicit nets. A check
  that rejected them outside port connections, gate terminals and
  continuous-assignment targets (6.10) was tried and withdrawn: system task
  arguments, `randomize` variable lists, specify blocks, `matches` pattern
  variables, `let` and constraint bodies all name things the unit does not
  declare, and it rejected valid designs. (Batch 41 adds the check, with
  each of these contexts handled.) Names of classes, `$unit`
  declarations, DPI imports, modports, covergroups and imported package
  types no longer become implicit nets.
- A SystemVerilog variable's constant declaration initializer takes effect
  before any process starts (6.8), with no time-zero event. The parser keeps
  the initializer as an initial process; elaboration moves its value into
  the variable's initial value, so the process then writes the value the
  variable already holds. Verilog-2005 units keep the time-zero write.
- A net whose only driver is a strong constant continuous assignment starts
  at the constant (initial procedures read it at time zero), unless it is
  forced, released, or waited on for any change (`always @*`, `@(n)`): ivtest
  `pr2986528` needs the time-zero change to wake an `always @*`, while
  Verilator `t_always_ff_never` needs an edge wait on a constant input not to
  fire. Both orders are races under the LRM (4.7).
- A non-ANSI module's ports bind positionally in header order. Before, body
  declarations reordered them, so `module m(a, b); input b; input a;` bound
  its first actual to `b`.
- A nested chain of `?:` lowered both arms again in its unknown-condition
  path at every level, doubling the work per level. Nested conditionals now
  lower each arm once, skipped when the condition excludes it, and merge
  with a conditional select.
- Omitted call arguments (`f(, 1)`) are dropped when the HIR is built, and
  the positional actuals after them are named by their formals. Both
  constant-function evaluators now bind named actuals by name.
- Static task and function locals are still per-process registers, not one
  shared variable, so a hierarchical reference to a task's local
  (`t.count = 0`) is not supported and two processes calling a static task
  see separate copies.
- `force net = expr` evaluates its expression once; a forced value does not
  follow later changes of the expression (ivtest `pr245`).
- `%v` (strength) is not implemented (17 ivtest cases).
- Packed arrays of packed structures (`s_t [3:0] a;`) are not represented;
  the dimension replaces the structure's width.
- VESTs reads of `iofile.*` depend on files written by other tests in GHDL's
  run order, in an implementation-defined binary format; they fail when each
  test runs alone.
- VHDL: an omitted port mode is `in`; `linkage` stays unsupported, which
  also keeps VESTs `tc120`-`tc125` (reading a `linkage` port) rejected.
  Aggregate assignment targets (`(a, b) := v;`) are split into one
  assignment per name: an aggregate value by element, a string literal by
  character, and an array object `v` by `v(v'left + k * sign)`.

## Analysis-time checks and operator overloads (batch 24)

- Most VHDL typing rules are checked where the elaborator types expressions,
  not in the analyzer. A VESTs analyzer-failure test is analyzed only, so an
  invalid unit that fails at elaboration still counted as accepted. `fsim
  compile` now elaborates each architecture it has just analyzed, as its own
  root, before committing the library, and reports diagnostics whose code is
  on an explicit list of analysis rules and whose location is in the
  analyzed files. Other elaboration diagnostics are ignored at this point:
  many come from missing design context (generic values given only to
  `elaborate`, unbound components) or from bounded-runtime limits that are
  not language errors (`FSIM-ELAB-VHPHYSICAL-006`, `FSIM-ELAB-HIR-001`). A
  code joins the list only after it fires on no compliant case in VESTs,
  nvc or the VHDL Compliance-Tests. An architecture that needs generic
  values is skipped, and so is one that instantiates anything (directly or
  in a generate statement): elaborating it would elaborate its whole subtree
  and made compiling the mixed-language benchmark designs four times slower.
  Packages are not elaborated this way.
- Where one diagnostic code covered both an analysis rule and an
  implementation limit, the limit moved to its own code: a condition or
  report expression whose type the elaborator cannot determine
  (`FSIM-ELAB-093`, `FSIM-ELAB-VHREPORT-003`), and a physical type with real
  bounds (`FSIM-ELAB-VHPHYSICAL-011`).
- `numeric_std` and `numeric_bit` arithmetic is provided natively, without
  visible operator declarations, so the predefined-operator check leaves
  array operands alone; it rejects records, enumerations, access values and
  `null` only when no function named like the operator is visible.
- A local subprogram hid every imported subprogram with the same
  designator. Only homographs hide (12.3): an imported overload stays visible
  unless a local declaration has the same parameter and result base types.
  When a local profile cannot be determined, the old rule still applies.

## Names, conversions and folding (batch 25)

- The unresolved-name check (`FSIM-ELAB-VHNAME-002`) looks only at simple
  names in value positions: operator operands, assignment values,
  conditions and the initial values of signals, variables and constants
  (interface defaults are checked only where used). Other name expressions
  in the HIR are not values: `others`, named-association formals, record
  aggregate element names and type marks. A name declared anywhere in the
  design is exempt, because the resolver does not model every form of
  visibility; so are generate parameters, enumeration literals (visible
  through an alias of their type), physical unit names, the STD.STANDARD
  and STD.ENV literals and the CHARACTER control literals. A design that
  includes the IEEE VITAL packages, whose declarations are compiler-supplied,
  is not checked. What remains detected are statement labels, design-unit
  names, record element names used alone, and names declared nowhere.
- A subprogram declared without a body (`FSIM-ELAB-VHBODY-001`) is looked
  up in the same design unit; methods of protected types are exempt, since
  their bodies are in the protected type body.
- A loop parameter is retained as an INTEGER constant without a value,
  whatever the type of its range, so the assignment type check treats such
  constants as untyped.
- Integer and floating type definitions are kept as subtypes of INTEGER and
  REAL, so two floating types declared with `range` are one type to the
  checks; converting between them is not checked.
- `conv(formal) => actual` on an output port is parsed like an individual
  subelement association of a port named `conv`. When `conv` is not a port
  of the declared component and the selector names an output or inout port,
  the association is rewritten through an implicit signal: the formal is
  associated with the signal, and concurrent assignments apply the
  conversions in each direction. Directly instantiated entities are not
  rewritten, because their ports are not known when the architecture is
  parsed.
- A user attribute's value is substituted at its use by the parser. The
  value of an attribute specification is a static expression evaluated where
  the specification is, so the substitution is exact for the corpora's
  forms; a specification in another design file is not seen.
- Constant folding treated every unary and binary VHDL operator as
  predefined. An operator that the design itself overloads (outside the STD
  and IEEE libraries) is now left as a call; `numeric_std` operators are
  still evaluated natively.

## TextIO, operator symbols and element names (batch 26)

- STD.TEXTIO is not analyzed from source before VHDL-2019; LINE, TEXT and
  OUTPUT are recognized by name. A LINE is held as a runtime string, so
  DEALLOCATE empties it, and an out or inout LINE formal is copied in and
  back out around the call. OUTPUT is the runtime's standard-output channel
  (multichannel descriptor 1). INPUT is not supported.
- WRITE of a value other than INTEGER, BOOLEAN, BIT or STRING appends the
  value's runtime image without quotes, as TO_STRING does. A value of a
  user-declared enumeration type is rejected, because STD.TEXTIO declares no
  WRITE for it. TIME values are counts of the resolution limit; a SimIR
  string method converts them to the requested unit at run time using the
  simulation's resolution, so the text does not depend on the project
  resolution. TIME'IMAGE uses fs, the primary unit, as nvc does (xsim prints
  in its own resolution unit).
- A call by operator symbol (`"op"(a, b)`, also selected from a package) is
  parsed as the operator expression. Overload resolution is then that of
  the operator, which is equivalent for the predefined and visible
  overloads. A selected operator from a package that is not otherwise
  visible resolves as if visible.
- A tick after a name, `)` or `]` is an attribute tick, never the start of a
  character literal, so `bit'('0')` lexes as a qualified expression.
- 'EVENT of a signal element is computed as the signal's 'EVENT and a
  comparison of the element with the same bits of 'LAST_VALUE. This is
  exact: in a delta cycle with an event, 'LAST_VALUE is the value before
  that event. 'ACTIVE, 'LAST_VALUE and 'LAST_EVENT of an element need
  per-element driver state and are still rejected.
- A process sensitivity list subscribes to the exact bits of a statically
  selected element or slice. An explicit `wait on` of an element waits on
  the whole signal, then waits again unless a named signal or element had
  an event. A `wait on` of a record field named by its selected name is
  still unsupported.

## Analysis checks and distinct scalar types (batch 27)

- `type T is range ...` is still lowered as a subtype of INTEGER or REAL,
  but its semantic type is recorded as a declaration (not a subtype), and
  the type checks root a subtype chain at such a type. Values of two such
  types, or of one and INTEGER, are distinct; universal literals match any
  of them.
- Most new VHDL checks run in the analysis-time elaboration of each
  compiled architecture and are whitelisted there. They are deliberately
  conservative: a check reports only when the operand types are classified
  with certainty, and calls to STD and IEEE subprograms are not checked
  against their formals, because STD.TEXTIO overloads are not retained.
- A multiple-index name such as `k(5,5)` is held as a single index operand
  in the HIR, so the number of indices is not checked.
- The VHDL-93 port association mode rules are checked in the frontend for
  ports of the enclosing entity named directly as actuals, against component
  declarations of the architecture and entities of the same compilation.
  VHDL-2008 relaxes them (buffer ports), so they are not applied there.
- All logical operators share one precedence in the VHDL parser, so a
  mixture of AND and OR without parentheses is reported rather than parsed
  with an invented precedence.
- Generate blocks: a concurrent statement or process of a generate block
  that names a member of an interface instance of the block is lowered
  after the unit's instances, as module-level statements already were.
- The automatic time resolution only ever refines the 1 ns default because
  of a literal; when a delay selects the resolution, a literal can make it
  finer (to ps or fs) but not coarser.

## Typed-file images and constant bounds (batch 28)

- Typed files are lowered per element as a list of scalar parts in the
  element's packed (most significant first) order. Reads of a fixed shape
  scan the parts from the file and then skip the rest of the line; reads of
  an unconstrained array scan from the line, unrolled up to the target's
  length (at most 4096 elements), and stop at the first part that does not
  convert. Integers of 32 bits or fewer accept a one-digit character literal
  image (`'1'`), as some VESTs iofiles contain one where an INTEGER file is
  read.
- TIME parts read as a magnitude and a unit; the resolution needed to turn
  them into ticks is not known to the lowerer, so it is obtained at run time
  from the fs image of one tick (`format_vhdl_time`).
- A CHARACTER part is read raw after one separator, since it may be a
  space; a standalone CHARACTER is written quoted so that `'` round-trips.
- The bounds of an unconstrained constant come from the evaluated value;
  for a string literal or positional aggregate the left bound is the index
  subtype's left bound (NATURAL for BIT_VECTOR, POSITIVE for STRING), not
  the packed layout's N-1 downto 0.
- `FSIM-ELAB-VHTYPE-001` covered both "not visible here" and "unresolved";
  only the first is reported at compile time (as `-006`), because the
  analysis elaboration does not resolve some IEEE subtypes such as X01.

## Array formals, slice aliases and NBA struct members (batch 29)

- An unconstrained array formal's frame range is captured when the actual
  holds `count * element_width` bits; the consumers of the range
  (`hir_expression_range`, the subtype the lowerer gives the formal, and the
  formal's width) accept any whole number of bits per index. A frame is keyed
  by the captured ranges, so calls with differently sized actuals do not share
  one.
- A slice or element alias stores its target as the HIR declaration's
  `initializer`; `alias_target` names the prefix. Lowering redirects a name
  of such an alias to its target expression, and an indexed name of it to an
  extract at the position in the alias's own range. The alias does not bind
  to the prefix object, so assignments through such an alias are not yet
  lowered.
- Runtime STRING values (variables, string formals, STRING constants) are
  indexed with `getc` and sliced with `substr`; a constant counts only when
  its subtype is STRING, since a BIT_VECTOR constant given by a string
  literal is also reported as string-valued by the expression check.
- A nonblocking member write reads the container object when it is
  published, applies the member write and writes the object back. Reading at
  publish time is what lets two member writes of one element in one time
  step both take effect.

## Character arrays, predefined bounds and analysis checks (batch 30)

- A VHDL string literal whose context expects 8 bits per character is an
  array of CHARACTER (a user `array (...) of character` type rather than
  STRING): each element is held as its character position, the leftmost
  element most significant. Other string literals keep the bit and
  STD_ULOGIC element encodings.
- REAL and TIME type marks are recognized as attribute prefixes without a
  HIR definition. TIME spans the 64-bit tick count, so TIME'HIGH is
  `INT64_MAX` ticks; `'LAST_EVENT` and `'LAST_ACTIVE` of a signal that never
  had an event or transaction return that value rather than the unsigned
  tick maximum. The TIME image converts ticks to femtoseconds in 128 bits,
  since TIME'LOW at a ps resolution overflows 64.
- Lexical conformance (IEEE 1076-1993 2.7) is checked before VHDL-2008
  only: the parser records the case-folded lexical elements of each
  subprogram specification from its designator. A body in the same region
  must repeat them when its formal type marks match a declaration; a
  package body's subprograms are compared when the package is linked.
- Linkage ports are parsed as inout with a `linkage` flag. The flag lets
  the validator reject reads and updates outside port associations and
  signal-attribute reads, and makes the port-mode association check accept
  any actual mode for a linkage formal.
- Constant names are folded to literals during normalization and lose
  their type there, so the slice range check cannot compare a constant
  bound's type with the index type (tc1125, tc1133 remain).
- Range-bound type checks compare enumeration roots only: the bounds of an
  integer or floating type definition may have different types, and such a
  definition is retained as a constrained subtype.
- A wildcard associative-array index (`[*]`) is held in 64 unsigned bits.
  Associative patterns with a `default:` member, and `foreach` over a
  `bit [63:0]` index, are not supported yet.
- A run-time `$clog2` smears the highest set bit of `x - 1` rightward and
  counts ones; `$clog2(0)` is 0.
- `obj.randomize();` and the other built-in randomization methods called as
  statements become expression statements, including inside class methods
  (the class executor evaluates them). The sv-tests that use them still stop
  at UVM `run_test`, which fails to lower `uvm_root::run_test`'s static
  locals (see the gaps queue).

## Resolution functions and default values (batch 31)

- A VHDL resolution function other than the `return a or b` / `and` forms
  makes its signal `ResolutionKind::vhdl_user_function`. After all processes
  are lowered, `validate_process_drivers` knows each signal's driving
  processes; a resolver program is then lowered per signal, in the scope
  that declared the signal (the binding keeps a copy of that
  specialization). The program calls the function once with a hand-built
  frame whose formal range is the formal index subtype's low bound up to
  low + N - 1, from an input register to an output register, and halts.
- The resolver is not scheduled. `Signal::vhdl_resolver` names its process,
  registers and driver processes. Resolution of such a signal at commit
  returns the last resolved value and marks the signal pending; after the
  update callback's commits, each pending resolver is run (the runtime fills
  the input register with the driving values, the first driver most
  significant, resets the program counter, executes the program, and commits
  the output with `commit`). A driver that has not written yet supplies the
  signal's declared initial value. At start the resolvers run once and the
  results become the initial values, without events.
- A signal without drivers keeps its declared value (no resolver).
  Guarded `bus`/`register` signals, which disconnect drivers and so change N
  at run time, and arrays of resolved elements (per-element resolution) are
  not supported yet.
- Default values: a VHDL record or array signal or variable without an
  initial value gets each scalar subelement's leftmost value (integer range
  left, NATURAL 0, POSITIVE 1, enumeration left, REAL'LEFT), the first record
  element and the left array element most significant. REAL objects start at
  REAL'LEFT. REAL subtypes keep their 64-bit packed range as an
  `array_index` constraint, which does not count as a range constraint here.
- Physical literals of a user physical type evaluate statically wherever
  the type is declared (the evaluator searched only the specialization's
  replaced types). Signal initializers given by a constant of a CHARACTER or
  REAL subtype, or by a constant with a static packed value, are evaluated.
- Member names of signals whose subtype is a resolved record subtype follow
  the subtype to its record definition.

## Entity constants, orderings and literal checks (batch 32)

- Constants of an entity declarative part are kept with the generics as
  local values; the HIR makes them constants, not generics. A signal
  assignment inside a subprogram declared in the entity is not an entity
  statement for the passive check.
- Function formals of class signal are accepted and passed by value (the
  formal keeps the signal object class); attributes such as `'event` of
  such a formal are not supported yet. `FSIM-ELAB-VHCALL-003` rejects a
  positional actual that is not a signal name.
- Ordering operators on one-dimensional arrays of discrete elements of
  equal length are lowered as a lexicographic chain from the left element
  (signed for INTEGER elements). Arrays of different lengths are still
  rejected (`FSIM-ELAB-VHARRAY-007`).
- Index constraints of array type definitions and the ranges of integer
  type definitions accept range attributes (`T range x'range`); a descending
  attribute range of an integer type definition is taken as ascending, since
  the integer range expression has no direction-from-bounds flag.
  `FSIM-ELAB-VHARRAY-012` rejects non-discrete index subtypes.
- A user attribute value may be indexed or sliced (`PT'AT1(2 to 4)`); an
  integer or Boolean value may not (`FSIM-VHDL-SEM-126`). Slices of slices
  parse, and a slice with constant bounds has that index range for a
  further slice or index.
- A call whose name is an object's record element (`v.re1(1)`) becomes an
  indexed name when the element exists in the object's record type.
- `'ACTIVE` of a statically indexed signal element is that of the whole
  signal, since transactions are tracked per signal.
- Port bindings treat BOOLEAN and BIT element domains alike.
- Decimal literals require decimal digits around an underline, and the
  digits of a based literal must be below the base (`FSIM-VHDL-LEX-005`).

## Aggregates, imports, standard vectors and element constraints (batch 33)

- TIME values are 64 bits wherever an array element is TIME, including a
  user array type named `time_vector`. A local variable whose index range
  has literal bounds keeps its declared layout; only a range given by
  nonstatic expressions takes its width from the initializer.
- A character literal element of an array of CHARACTER is its character
  code, not a logic value, and a logic literal has no width of its own.
- The package import check accepts `use lib.all`, `use lib.unit` (a
  package or another design unit) and, after either of those, `use
  pkg.all` or `use pkg.item`.
- A qualified aggregate whose type is declared in a process or subprogram
  finds that type by its simple name: the unique type of that name, or the
  one declared in the aggregate's scope.
- Ordering operators on discrete arrays accept operands of different
  lengths (a proper prefix is less) and aggregate operands, which take the
  array base type of the other operand.
- INTEGER_VECTOR, BOOLEAN_VECTOR, TIME_VECTOR and REAL_VECTOR support
  aggregates, indexed reads and writes, and element defaults (`'LEFT`) for
  signals and variables.
- An access value is a 32-bit handle in a record. Assigning to an
  access-typed record element writes that element only; before, the
  access-assignment path replaced the whole record.
- Element constraints (VHDL-2008): `T(r)(r2)`, `T(open)(r2)`, `R(e(r))`,
  nested forms, and element constraints carried through a named subtype.
  The HIR subtype keeps a list of constraints, each addressed by a path of
  array steps (`""`) and record element names. They are applied in subtype
  widths, signal layouts, object checks, array element and record member
  selections, aggregates, default values and attribute prefixes. From
  VHDL-2008, an array type definition may have an unbounded element
  subtype.
- Not yet: element constraints that a subprogram formal takes from its
  actual (`r(i)'range`, `'element` inside the subprogram), ports whose
  element constraints come from the actual, and element-constrained
  constants inside protected types.
- VHDL resolution programs always run in the interpreter. The compiled
  engine no longer JIT-compiles them; it rejected the externally filled
  input register.
- nvc issue616 now elaborates. It expects a run-time length error for a
  17-bit `to_signed` value in a 16-bit record element of an unused
  constant, and fsim does not check this.

## Element constraints from actuals, labels and attribute specifications (batch 34)

- An unbounded array formal takes its actual's element constraints as
  well as its index range: the call frame keeps them per formal, and frames
  are specialized by them. `x(i)'range` inside the subprogram is then
  bounded. The type check accepts array attributes of elements of a port or
  subprogram formal, whose bounds come from the actual.
- Element constraints also apply when computing index ranges, constant
  selections, static signal initializers with nested aggregates, and
  element subtypes reached through dynamic indexes and member selections.
- INTEGER_VECTOR, TIME_VECTOR and REAL_VECTOR elements are 32 or 64 bits
  wherever an index range is derived from a width, and their static
  aggregates nest (an array of INTEGER_VECTOR initialized by an
  aggregate). Constant selections of VHDL arrays use the element width,
  not one bit per index. Before, `t(1).x(2) := 8` on an INTEGER_VECTOR
  record element in an array of records did not lower. Variable and
  signal defaults of these vectors inside records are `'LEFT`.
- Attribute specifications (`FSIM-ELAB-VHATTR-016`): an entity of another
  class (subprogram formals accept any object class), an undeclared
  entity, a selected name, an entity named after `others` or `all` for the
  same attribute and class, and an integer value for a floating attribute
  (or the reverse) are rejected. Specifications in entity and
  architecture declarative parts, including processes, are checked.
- A call of an enumeration-indexed table with one character literal per
  dimension (`t('1', '0')`) becomes an indexed name, and character literal
  indices of BIT, CHARACTER and STD_ULOGIC dimensions use their positions.
  Two-dimensional constants with bit or logic elements still do not lower.
- SystemVerilog: a statement label before `begin` or `fork` (`name: begin
  ... end: name`) names the block. A class whose parameter has no default
  gets no default specialization. Such a class is legal while unused, but
  `new` for a variable of a specialized class type (`box #(5) b; b = new;`)
  still constructs the default specialization.

## Tables, case choices, 'SUBTYPE and final procedures (batch 35)

- A multidimensional constant of BIT or STD_ULOGIC elements given by fully
  positional nested aggregates of character literals is folded and indexed
  (`and_table('L', '1')`). Aggregates with `others` or named choices are
  not folded yet.
- A character literal case choice of a BIT selector (`when '0'`) was
  rejected by the domain check (`FSIM-ELAB-VHDLCASE-001`), because the
  literal's own domain differs from BIT's; character literal choices now
  take the selector's type.
- `obj'subtype` as a subtype indication (VHDL-2008) denotes the subtype of
  an earlier visible object, including the entity's ports. An
  unconstrained array object, such as a subprogram formal, gets one
  `obj'range` constraint per dimension, so the call frame's range applies.
  Element constraints that a record formal takes from its actual are not
  carried yet.
- `'LENGTH`, `'LEFT`, `'RIGHT`, `'LOW` and `'HIGH` of a STRING held as a
  runtime string (`variable result : string(1 to size)` in a function) are
  computed from its length and static left bound. A `for` loop over such a
  string's `'RANGE`, and element writes to it, still fail.
- SystemVerilog: `$finish` and `$stop` in a final procedure are legal and
  end the simulation at once (IEEE 1800-2017 9.2.3; before, the parser
  rejected them with `FSIM-SV-SEM-032`). A `$stop` executed by a final
  procedure prints "$stop paused the simulation" like any other `$stop`.
- A function name without actuals is a call wherever a STRING is expected
  (`report color_start;`). STRING record elements in reports (`c1.f`) are
  not supported yet.
- `'LAST_VALUE`, `'DRIVING_VALUE` and `'DELAYED` have their signal's
  subtype, so an aggregate compared with them takes that type.
- BIT_VECTOR and BOOLEAN_VECTOR operands of different lengths are ordered
  lexicographically (`"00" < "000"`); before, they were compared as
  numbers. A string literal operand takes the array type of the other
  operand.
- SystemVerilog: named events declared in generate blocks. An unsupported
  generate-block item made the parser report the same error forever
  without consuming input (ivtest `scoped_events` used 12 GB at compile);
  the generate declaration parser now always advances.

## Analysis checks, STD.ENV imports and TEXTIO TEE and FLUSH (batch 36)

- A procedure call that names no visible subprogram is rejected at
  analysis with `FSIM-ELAB-VHPROC-014`, which names the procedure (IEEE
  1076-2008 10.7). Before, the lowerer gave up silently and the case
  failed later with the generic `FSIM-ELAB-HIR-001`.
- `use std.env.all` (or `use std.env.stop`) makes `stop` and `finish`
  callable by their simple names. Under VHDL-2008 the STD.ENV package has
  no declarations in the design, so a simple name that a use clause
  imports from STD.ENV, and that no declaration overloads, takes its
  selected spelling for the simulator API checks. The STD.ENV directory
  procedures use the same mapping.
- A guarded block's implicit GUARD signal has no sources: a concurrent
  assignment to `guard` in a block without its own GUARD declaration is
  `FSIM-VHDL-SEM-127`. A guard expression made only of integer literals
  and arithmetic is not BOOLEAN (`FSIM-VHDL-SEM-128`).
- The reserved word `all` is not an entity, architecture or package name.
- Unresolved CHARACTER control literal names (`nul`) have type CHARACTER,
  so assigning one to a BIT is a type error. Case and selected-assignment
  choices that name nothing visible are `FSIM-ELAB-VHNAME-002`.
- A selected element name whose object the reference did not select
  (`v.item`) takes its subtype from the prefix object, so assigning an
  integer to a record element of record type is rejected.
- TEXTIO `TEE` (VHDL-2008 16.4) writes the line to the file and to
  OUTPUT and empties it. `FLUSH` (VHDL-2008 5.5.2) flushes an open file
  or OUTPUT; a second file object opened on the same path then reads what
  was written. `READ` and `WRITE`, `TEE` and `FLUSH` count as file
  operations only when their first actual is a file object or line.
  `SREAD`, `JUSTIFY` and reads of STRING values from a line are not
  supported yet.
- A signal initializer that does not fold at elaboration (a function
  call, or a composite constant that the static evaluator cannot read)
  is still rejected with `FSIM-ELAB-HIR-001` ("is not a bounded integral
  value"). VHDL needs these values before the first delta, so a
  SystemVerilog-style initializer process writing at time 0 would add an
  event; about 22 VESTs and nvc cases are blocked.

## Forces, bidirectional connections and strengths (batch 37)

- A procedural `force` keeps its expression applied until the target is
  released or forced again (IEEE 1800-2017 10.6.2). Before, the value was
  forced once, so `force w = v` never followed later changes of `v`.
  Forces now use the procedural-assign machinery: a per-target owner
  signal names the active force, and a driver process re-forces the value
  while it owns the target. A force of one element or slice still shares
  the owner with other forces of the same signal.
- A force overrides a procedural `assign` of the same variable: the
  assignment neither applies nor re-applies while the force is active,
  and takes effect again when the force is released.
- An `inout` port whose actual is a constant bit- or part-select of a net
  (`buffer u(bus[0], ...)`) joins the selected bits through a
  bidirectional switch, as an SV `alias` slice does. Before, the formal
  was a separate net connected by a copy in each direction; that loop
  held an unknown value once both sides had seen one.
- `tran`, `rtran`, `tranif0`, `tranif1`, `rtranif0` and `rtranif1` are
  resolved only by the runtime switch network. Their directional
  assignments used to run too and became drivers, so a terminal's own
  value was fed back to it (a `tran` never returned to `z`). Every switch
  network is solved once at the start of simulation, so supply nets reach
  the network without a driver change. A forced net drives the network
  with its forced value at strong strength. A non-resistive switch passes
  strength unchanged except that supply becomes strong.
- Strength resolution keeps unknown values apart from known ones: an `x`
  driven with unequal 0 and 1 strengths (`assign (supply1, strong0) w =
  x`) is an ambiguous `x`, not the stronger value, and a known value wins
  only when it is stronger than every opposing component. A value that
  passes a switch with an unknown control becomes L or H.
- `%v` (21.2.1.5) prints each bit's strength and value, most significant
  bit first and separated by `_`, using the mnemonics and the two-digit
  ranges of the standard (`St0`, `HiZ`, `67X`, `StL`, `760`). MOS and CMOS
  switches with an unknown control do not produce L or H values yet, and
  a switch delay is not applied.
- Class constructors receive their string arguments (`new("name")`): the
  runtime passed only the packed actuals to the constructor body, so a
  string formal read as empty. `super.new(...)` evaluates string actuals
  as text for a string formal of the base constructor.
- `super.new(...)` in a class compiled separately from its base class
  (the base is in an earlier compilation or a mapped library) is a base
  constructor call; it was resolved as an ordinary method call of `new`
  and failed at run time with "formal has no actual or default".
- A virtual function call through a pure virtual prototype
  (`base_cls h; h.print();` with `pure virtual function void print();`)
  dispatches to the object's override (8.21); before, the call was not
  lowered. A pure virtual task called this way is still not supported.
- A method of an implemented interface class (`courier.deliver()` with
  `interface class Courier`, 8.26) dispatches to the method of the same
  name in the object's class, which the base chain does not reach.
- String-returning class methods (`c.get_name()` used as a string) are
  not supported yet: the class executor and `ClassMethodCall` carry only
  packed results.

## String methods, compound updates, arrays of arrays, record constants and 'VALUE (batch 38)

- A class method that returns a string can be used as a string value
  (`s = c.get();`, `c.get() == "x"`, `$display("%s", c.get())`). The
  result travels to the caller in a trailing hidden string actual
  (`@result-string`), which the runtime copies back like an output string
  argument, so `ClassMethodCall` needed no new result field. A string
  function called from inside another class method still discards its
  result.
- Compound assignments and increments of unpacked array elements selected
  by variable indices, and of multidimensional elements (`a[i] += 1`,
  `m[1][2]++`), run as the ordinary assignment of their normalized value
  when the target has no side effect. A target with a side effect
  (`x[i++] += 2`) still needs a single evaluation and is not supported.
- An element of a multidimensional array whose elements are arrays
  (`m(2, true)`) has the element subtype: each selector consumes one
  dimension. The lowerer and the analysis checks used to strip a single
  dimension. An element of a multidimensional record member read with
  constant indices (`v.f(1, false)`) is a static slice of the member.
- Elements of record constants (`c.f1`, `c.f2(2)`) read the constant's
  initializer, an aggregate given its declared subtype. Before, a record
  constant's elements could not be read at all.
- `T'VALUE` of integer types (whitespace, sign, underscores, based
  literals and exponents) and of enumeration types (case-insensitive
  identifiers, character literals), including BOOLEAN, BIT and
  SEVERITY_LEVEL. `T'VALUE` of CHARACTER, real and physical types is not
  supported yet.
- UVM (partly): `$cast` to a library class no longer keeps the class from
  specializing, so user classes derived from `uvm_component` and its
  subclasses are available; user constructors run after the UVM services
  have initialized the object and component state, and `super.new` stops
  at the UVM library classes; `uvm_report_enabled`,
  `get_report_verbosity_level` and `get_report_action` answer from the
  report policy; `super.*_phase` calls into `uvm_component` are not run.
  The class executor now passes string actuals, evaluates `&&` and `||`,
  unsized based literals and package-qualified enumeration literals. A
  UVM test still stops at `run_test()`, whose library body the class
  executor cannot run (`uvm_init`); the simulation's native test runner is
  not connected to it yet, and the sv-tests UVM cases that pass are
  elaboration checks.
- A shift in a Verilog-2005 or SystemVerilog expression takes the context
  width (IEEE 1364-2005 5.4.1): `{a, b} >> 4` assigned to a wider target
  shifts the widened value. The self-determined width was used for every
  language except SystemVerilog, so Verilog-2005 dropped high bits.
- `$printtimescale` and the stochastic queue tasks (`$q_initialize`,
  `$q_add`, `$q_remove`, `$q_full`, `$q_exam`) are Verilog-2005 tasks as
  well (IEEE 1364-2005 17.3.1, 17.6); they were rejected outside
  SystemVerilog.
- A task or function declared in a package or in `$unit` uses its
  declaring scope's time unit for `$time`, `$stime` and `$realtime`, and
  `$printtimescale` without an argument names that scope (`($unit::)`,
  `(pkg::)`; IEEE 1800-2017 3.14.2, 20.4.1). `$unit` takes the
  compilation-unit `timeunit`, or the `` `timescale `` in effect. A class
  declared in `$unit` still uses the caller's time unit.
- A `return` value converts to the function's declared type like an
  assignment (IEEE 1800-2017 13.4.1): a real returned from an integral
  function rounds to the nearest integer, and an integral value returned
  from a real function becomes real. Both returned the raw bits before.
- Assignment to an element of a VHDL STRING variable (`s(2) := 'x'`),
  which is held as a runtime string, replaces the character at the
  index's offset from the declared left bound. It was rejected.
- A VHDL constant initialized by an `others` aggregate is evaluated with
  its declared subtype as the aggregate's context, and a CHARACTER
  constant fills an array of CHARACTER as its 8-bit code. Arrays of
  CHARACTER other than STRING still cannot be initialized from named
  constants.
- A qualified expression of an array type (`t'(...)`) is no longer
  mistaken for an indexed name when the analysis checks look for an
  array root; only object names are indexed that way, and a slice keeps
  its array type.
- Individual association in procedure calls (IEEE 1076-2008 6.5.7.1):
  `p(1) => a, p(2) => b`, `p.x => a`, `p(1, 2) => a`, `p(1)(2) => a`.
  The parser gathers the associations of one formal into a named
  aggregate actual (`p => (1 => a, 2 => b)`), nested one level per
  selector. An `in` subelement takes the aggregate's value; for `out` and
  `inout` formals the returned value is copied back through the aggregate
  to each named actual. Conversion functions in the formal part
  (`f(b) => x`) and signal-class formals are not supported yet.
- An `out` or `inout` actual that is a record element (`v.f`) now updates
  only that element; the whole record was overwritten with the element
  value before. An element of a multidimensional array with static
  indices (`m(2, 1)`) is accepted as such an actual.
- Expanded names whose prefix is an enclosing process, block, loop or
  subprogram label, or the architecture or entity name (`proc.v`,
  `outer.i`, `arch.s`; IEEE 1076-2008 8.3), select that construct's
  declaration. They were looked up as package members.

## Port conversions, bindings, generics, matching operators and packed elements (batch 39)

- A port actual that is a conversion function call (`f => conv(s)`)
  creates an adapter signal with the formal's materialized type: its
  domain, signedness, integer range, array metadata and default value. An
  array of INTEGER formal failed the boundary checks before (value-domain
  and signedness mismatches), and its elements could not be read through
  the adapter. Arrays of INTEGER are no longer checked as one signed
  integer at a port boundary, also for direct signal actuals.
- A binding indication with a generic or port map (in a configuration or
  a configuration specification) associates the entity's formals itself,
  so the component it binds need not declare the same generics.
- REAL generic actuals that are static (`10.0`, `real(n'high)`) are
  accepted, and a REAL generic is a real operand in arithmetic with other
  real expressions.
- An expanded name prefixed by the entity name must denote a declaration
  of the entity (`ent.sig` for an architecture signal is an error,
  `FSIM-ELAB-VHNAME-014`), as an analysis check.
- Generic subprograms in standard order (`function f generic (n :
  integer) (x : integer) return integer`), with an optional `parameter`
  keyword. The generic clause is moved in front of the subprogram keyword
  before parsing, where the existing generic subprogram support reads it.
- Operator-symbol designators of interface functions (`function "+" (a,
  b : t) return t is <>`) parse; resolving their `is <>` defaults to the
  predefined operators of the actual type is not supported yet.
- The name of a unit of a user physical type used alone (`yd`, `ft`)
  denotes one unit. Predefined TIME units used alone (`us = 1000 ns`) are
  still not supported.
- Matching ordering operators `?<`, `?<=`, `?>`, `?>=` of BIT and
  STD_ULOGIC (IEEE 1076-2008 9.2.3): 'U' when an operand is 'U', 'X' when
  an operand is not 0, 1, L or H, otherwise the order of the 0/1 values.
  The predefined matching operators have the operands' element type (BIT
  or STD_ULOGIC) for overload resolution.
- Overload resolution of procedure calls: a character literal actual
  matches only a character type that contains it (BIT for '0' and '1',
  STD_ULOGIC for its nine values, CHARACTER), never BOOLEAN, an integer
  type or an array; a scalar BIT or STD_ULOGIC actual does not match an
  array formal.
- SystemVerilog: an index of an unpacked array element selects the
  element's packed dimensions (`logic [1:0][3:0] p [0:1]; p[1][0]` is a
  4-bit element, `p[1][1][2]` a bit). The first packed dimension was
  dropped for the unpacked index, so `p[1][0]` read and wrote one bit.
- The VUnit stand-in's protected functions now each consist of a single
  return statement (their state changes moved to protected procedures), so
  testbenches that loop with `while test_suite loop ... run(...)` run.
- Variables of the predefined VHDL-2008 vectors (`INTEGER_VECTOR`,
  `REAL_VECTOR`, `TIME_VECTOR`, `BOOLEAN_VECTOR`) without an initializer
  have storage: they have no HIR type definition, so their index
  constraints now mark them as composite storage, and `REAL_VECTOR` has
  64-bit elements. Wider than 64 bits, they were rejected before.
- A conversion-function adapter starts with the formal type's default
  value (each element's `'LEFT`) until the conversion runs. The LRM
  computes the initial effective value through the conversion function;
  fsim does not yet, so a process that reads the formal at time zero sees
  the default.

## Package instances, blocks and literal overloads (batch 40)

- A design-unit package instantiation (`package p is new work.g generic
  map (...)`, IEEE 1076-2008 4.9) whose generic package is declared in the
  same design file is expanded by the parser into an ordinary package and
  body named `p`: a constant generic becomes a constant declaration with
  its actual (or default), a type generic a subtype of its actual, and a
  subprogram generic a wrapper subprogram that calls its actual. A
  library-qualified subprogram actual (`work.q.f`) adds `use work.q.all`
  to the expanded package, because fsim does not yet resolve
  library-qualified calls without a use clause. Interface package
  generics, `generic map (<>)` and templates in other design files (such as
  `ieee.fixed_generic_pkg`) still use the declaration path, which does not
  support them.
- Package instantiations inside package declarative regions parse into
  local package instances.
- Procedure overload resolution: an abstract literal without a unit is
  not of TIME (`check_equal(17, 17)` no longer also matches the TIME
  overload); folded time literals keep their TIME type.
- `label : name;` inside a block or generate body instantiates a declared
  component without maps (it was taken as a procedure call).
- Configuration specifications in block declarative parts are applied to
  the block's instances, and `use open` leaves a component instance
  unbound (IEEE 1076-2008 7.3.2.2); before, `open` selected the default
  binding.
- Analysis checks: a label, architecture name or entity name used as a
  generic or port map actual is rejected (`FSIM-ELAB-VHNAME-015`), and a
  nested block configuration must name a block or generate statement of
  the enclosing block (`FSIM-FE-VHCONFIG-001`). Architectures that
  instantiate other units are not elaborated at compile time, so their
  name checks (`VHNAME-002`, `-004`, `-015`) now run on their own at
  compile.

## Undeclared identifiers and VHDL analysis checks (batch 41)

- An undeclared simple identifier is an error (`FSIM-SV-SEM-411`) unless
  its context declares an implicit net: a port or terminal connection or
  a continuous-assignment target (IEEE 1364-2005 4.5, IEEE 1800-2017 6.10).
  The contexts that made batch 23 withdraw the check are handled
  separately:
  - system task statement arguments may name scopes (`$dumpvars(1, top)`,
    `$printtimescale(top)`), so they keep the old implicit-net behaviour;
  - `obj.randomize(v)` lists and `with` constraints, out-of-block
    constraints, and out-of-block method bodies (`C::m`) name class
    members, so their references are dropped;
  - specify blocks keep implicit nets, and their terminals are checked by
    the specify elaboration;
  - `matches` pattern variables and `let` formals are names of the item
    and the `let` body;
  - an identifier in an associative-array dimension (`[key_t]`) is an
    index type; specparams are declarations;
  - event controls keep implicit nets, so `@seq` on a named sequence still
    elaborates as before;
  - `bind` actuals resolve in the target scope;
  - a virtual interface initializer is checked when it is bound.
  `new` and `$` (an unbounded range end) are never identifiers. `$` in an
  `inside` range now leaves that end unbounded (11.4.13).
- Before SystemVerilog:
  - a gate or switch primitive's output or bidirectional terminal must be
    a net (`FSIM-SV-SEM-412`, IEEE 1364-2005 7.1);
  - a concatenation cannot hold an unsized constant (`FSIM-SV-SEM-413`,
    5.1.14);
  - a non-ANSI port and its data declaration have identical ranges, so a
    scalar port cannot become a vector (12.3.3).
- Every port named in a non-ANSI port list is declared in the body
  (`FSIM-SV-SEM-414`); `$signed` and `$unsigned` of a real literal are
  rejected (`FSIM-SV-SEM-415`).
- VHDL analysis checks, mostly from the VESTs `analyzer_failure` cases:
  - a case choice that names a variable or signal (`VHDLCASE-011`);
  - a procedure called in an expression (`VHCALL-004`);
  - a loop range with REAL, TIME or physical-unit bounds, or real
    arithmetic (`VHLOOP-001`);
  - a type definition range that reads a port, signal, variable or
    generic (`VHRANGE-004`). `type T is range ...` is kept as a subtype
    declaration of a new semantic type, which tells it from `subtype S is
    integer range ...`;
  - a type mark as a range bound (`VHRANGE-005`);
  - index constraints whose bounds are not of the index type, an
    anonymous enumeration range with literals of two visible types, and
    NATURAL- or POSITIVE-indexed array types with out-of-range bounds. An
    anonymous index range carries a placeholder INTEGER index subtype
    whose span is its left bound, which identifies it;
  - a physical unit whose position lies outside its type's range;
  - `'0' = '0'` (BIT or CHARACTER, `VHOPER-003`);
  - an alias with a subtype indication naming a type (`VHALIAS-004`);
  - a loop parameter used after its loop;
  - a local of another unit's subprogram, which is no longer treated as
    possibly visible;
  - an attribute specification before its entity's declaration;
  - duplicate and out-of-range integer choices of a one-dimensional
    aggregate;
  - a pre-2008 port actual indexed by a signal (`VHPORT-005`);
  - a procedure declared without a body in its region when a nested block
    has a body of the same name;
  - an undeclared name in a waveform delay.

  Name-independent codes (`VHCOMP-016`, `VHCALL-004`, `VHDLCASE-011`,
  `VHPORT-005`) are also reported for architectures that instantiate
  other units.
- An unresolved VHDL INTEGER signal with two drivers is reported
  (`FSIM-ELAB-DRV-001`); the exemption for Verilog `integer` variables no
  longer covers VHDL signals.
- An unconstrained array constant takes its length from its value: its
  width was the element width, so only `c(0)` lowered. Its index range
  starts at the index subtype's left bound.
- Interface members by hierarchical name:
  - a concurrent assertion clocked by an interface member
    (`@(posedge dif.clk)`) takes that member as its clock;
  - assertions may name instance members;
  - a name such as `dif.data` that is not a local binding resolves as a
    hierarchical signal (retried once later instances exist), also as a
    task output actual;
  - a virtual interface held in a class property is read from the
    property.
- A selected name whose prefix is a block, process or generate label of
  the architecture is not an expanded package name; an indexed name with
  a scalar prefix is rejected (`VHARRAYSEL-005`).
- A string literal or positional aggregate actual of an unconstrained
  array formal has the index range of the formal's index subtype.

## Tagged unions, guarded events and VHDL predefined operations (batch 42)

- `@(e iff g) S` waits for `e` until `g` holds, then runs `S` (IEEE
  1800-2017 9.4.2). It lowers to `do @(e); while (!g);`, so one guarded
  event per control is supported; several are reported
  (`FSIM-SV-UNSUPPORTED-048`).
- `if (e matches p) s1 else s2` is parsed as `case (e) matches p: s1;
  default: s2; endcase` with generated tokens (12.6.2). Pattern variables
  are procedural names of the item, so they are not undeclared
  identifiers.
- A tagged union `void` member is a one-bit placeholder; `tagged X`
  without a value stores only the tag. Reading a member other than the
  current tag asserts with severity error (11.9).
- `disable soft v` lowers to a call `@disable-soft` that drops the soft
  constraints on `v` from the solver (18.5.14.2).
- Compilation-unit checks: duplicate generate or block names (`SEM-416`),
  attribute values that are not constant (`SEM-417`; FSM pragma names
  are exempt), and `$unit` typedefs naming undefined types (`SEM-418`).
  The typedef check runs only when the unit imports nothing but `std`, so
  names from other packages are not falsely reported.
- VHDL MINIMUM and MAXIMUM (1076-2008 5.3.2.4) are lowered when no
  user-visible function of that name exists: two scalars through a
  compare-and-select, or a reduction over the elements of a
  one-dimensional array. A null array gives the element type's HIGH
  (MINIMUM) or LOW (MAXIMUM). Two array operands (lexicographic order)
  are not yet supported (nvc `predef2`).
- TO_STRING (5.7): REAL with DIGITS or a printf FORMAT, TIME with UNIT
  (the fraction is written to 15 significant digits, like a REAL image),
  user physical types (value in the primary unit), and arrays of
  character-enumeration literals (each element's character).
- A string literal of an array of a character enumeration holds each
  character's position; without a contextual type it uses the one
  encoding all such array types of the unit agree on.
- `?=` gives `'X'` when an element pair holds a metavalue and none
  differs (9.2.3); the interpreter, word kernel and JIT agree.
- A logical operator with one BIT or STD_ULOGIC operand and one array
  operand replicates the scalar across the array (9.2.2).
- An array whose element is a constrained STRING subtype is valid before
  VHDL-2008. Its elements are packed CHARACTER positions; reading one as a
  STRING formats those bytes, and a static concatenation of literals in
  that context is folded to the bytes.
- Timeout order (14.7.5.3): a VHDL process resuming from `wait for` is
  queued from the update phase of its time, after that time's driver
  updates, with the processes they wake. It used to resume first, so it
  read the old values. VITAL timing checks compare event times: a test or
  reference event earlier in the current time that did not wake the check
  (a `'DELAYED` copy) still counts.
- A sec, min or hr design time resolution is accepted by the VPI time
  profile.
