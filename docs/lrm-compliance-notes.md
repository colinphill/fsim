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
