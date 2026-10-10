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
  declare, and it rejected valid designs. Names of classes, `$unit`
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
