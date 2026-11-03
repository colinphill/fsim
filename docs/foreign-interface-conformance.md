<!-- SPDX-License-Identifier: Apache-2.0 -->
# Foreign-interface conformance

This inventory lists the normative items of the SystemVerilog VPI (IEEE 1800-2023
clauses 36–38, Annex K) and of the VHPI (IEEE 1076-2019 clauses 17–23, Annex B)
and records how fsim implements each one. It drives the conformance work that
precedes first-class cocotb support; rows change status as items close.

Status values:

- **done**: implemented as the clause reads, with a test.
- **partial**: implemented with a stated gap.
- **missing**: not implemented; the routine or property raises an error.
- **N-A**: does not apply to fsim, with the reason.
- **choice**: the clause leaves the behaviour to the tool; the row states fsim's choice.

Files: `vpi` = `src/app/application_vpi_foreign.cpp`, `vpi-cb` =
`src/runtime/vpi_callback.cpp`, `vpi-val` = `src/runtime/vpi_value_control.cpp`,
`vpi-std` = `src/runtime/vpi_standard.cpp`, `vhpi` =
`src/app/application_vhpi_foreign.cpp`, `vhpi-std` = `src/runtime/vhpi_standard.cpp`,
`run` = `src/app/application_run_commands.cpp`.

## Loading model

| Item | Clause | Status | Where | Notes |
| --- | --- | --- | --- | --- |
| `vlog_startup_routines` table | 1800 36.9.1 | done | vpi | null-terminated table; a `lib:function` spec names a bootstrap function instead |
| startup restrictions | 1800 36.10.2 | partial | vpi | the registration host accepts any callback reason |
| `vhpi_startup_routines` / bootstrap | 1076 20.2 | done | vhpi | |
| registration-phase restrictions | 1076 20.2.2 | partial | vhpi | `vhpiRootInst` is NULL during registration |
| when libraries load | — | choice | run | `elaborate --vpi` registers user system tasks; `simulate --vpi/--vhpi` runs the startup routines once and gives the library every callback and value service |

## VPI routines (IEEE 1800-2023 38)

| Routine | Clause | Status | Notes |
| --- | --- | --- | --- |
| `vpi_chk_error` | 38.2 | done | every failing routine leaves an error naming the routine; file and line are not recorded |
| `vpi_compare_objects` | 38.3 | done | |
| `vpi_control` | 38.4 | partial | `vpiFinish`, `vpiStop`; diagnostic level ignored; `vpiReset`, `vpiSetInteractiveScope` missing |
| `vpi_flush` / `vpi_mcd_flush` | 38.5 | partial | flushes stdout only |
| `vpi_get` / `vpi_get64` | 38.6 | partial | property gaps listed in the object-model table |
| `vpi_get_cb_info` | 38.8 | partial | not available once a one-shot callback has fired and been retired |
| `vpi_get_data` / `vpi_put_data` | 38.9 | missing | save/restart (gate A-G6) |
| `vpi_get_delays` / `vpi_put_delays` | 38.10 | done | module paths |
| `vpi_get_str` | 38.11 | partial | property gaps listed in the object-model table; `vpiConfig` is NULL (configurations are not published to VPI) |
| `vpi_get_systf_info` | 38.12 | done | |
| `vpi_get_time` | 38.13 | done | |
| `vpi_get_userdata` / `vpi_put_userdata` | 38.14 | done | |
| `vpi_get_value` | 38.15 | partial | `vpiStrengthVal` missing; reads come from the registry mirror (gate A-G7) |
| `vpi_get_value_array` / `vpi_put_value_array` | 38.16 | done | |
| `vpi_get_vlog_info` | 38.17 | done | product `FSIM` (as `std.env.TOOL_NAME`), version is the fsim release version |
| `vpi_handle` | 38.18 | partial | see the object-model table |
| `vpi_handle_by_index` | 38.19 | done | |
| `vpi_handle_by_multi_index` | 38.20 | partial | |
| `vpi_handle_by_name` | 38.21 | partial | a scoped lookup falls back to a global one |
| `vpi_handle_multi` | 38.22 | missing | |
| `vpi_iterate` / `vpi_scan` | 38.23, 38.37 | partial | an iterator is released after its last object |
| `vpi_mcd_open` / `vpi_mcd_close` | 38.24 | missing | gate A-G6 |
| `vpi_mcd_name` / `vpi_mcd_printf` | 38.24 | partial | channel 1 (stdout) only |
| `vpi_printf` / `vpi_vprintf` | 38.25 | done | |
| `vpi_fopen` / `vpi_get_file` | 38.26 | missing | gate A-G6 |
| `vpi_put_value` formats | 38.34 | partial | `vpiStrengthVal` and `vpiObjTypeVal` missing; a string onto a real is not rejected |
| `vpi_put_value` `vpiNoDelay` | 38.34 | done | immediate deposit |
| `vpi_put_value` `vpiInertialDelay` | 38.34 | done | a new write removes every pending write on the object |
| `vpi_put_value` `vpiTransportDelay` | 38.34 | done | modified transport: removes pending writes due after the new one |
| `vpi_put_value` `vpiPureTransportDelay` | 38.34 | done | removes nothing |
| `vpi_put_value` `vpiForceFlag` / `vpiReleaseFlag` | 38.34 | done | |
| `vpi_put_value` `vpiCancelEvent` | 38.34 | done | cancels a pending write and frees its handle; cancelling an applied write is not an error |
| `vpi_put_value` `vpiReturnEvent` | 38.34 | done | returns a `vpiSchedEvent`; `vpiScheduled` is 1 while pending |
| `vpi_register_systf` | 38.35 | done | |
| `vpi_register_cb` | 38.36 | partial | reasons listed below; live registrations are bounded at 1,048,576 |
| `vpi_release_handle` / `vpi_free_object` | 38.38 | partial | iterators and `vpiSchedEvent` handles; freeing a scheduled event does not affect it |
| `vpi_remove_cb` | 38.39 | done | |

## VPI callback reasons (IEEE 1800-2023 38.36)

| Reason | Status | Notes |
| --- | --- | --- |
| `cbValueChange` | done | fires in the region of the change with the captured new value; NULL value for an event; `index` set for an array word |
| `cbForce`, `cbRelease`, `cbAssign`, `cbDeassign` | done | repetitive; after the statement runs, at the next safe point, with `obj` the force, release, procedural assign or deassign statement and `value` the target's resulting value, of the bits the statement acts on (38.36.1). A NULL `obj` at registration watches every statement, a net or variable only those on it; a bit-select is refused. The processes that keep a forced or assigned value applied are not statements and make no callback (`fsim.foreign.vpi-statements`) |
| `cbStmt`, `cbDisable` | missing | gate A-G5: `cbStmt` needs a probe before each statement in the compiled kernel; `cbDisable` a hook on disable |
| `cbCreateObj`, `cbReclaimObj`, `cbEndOfObject`, `cbSizeChange`, `cbStartOfFrame`, `cbEndOfFrame`, `cbStartOfThread`, `cbEndOfThread`, `cbEnterThread` | missing | gate A-G5: they act on class objects, dynamic arrays, frames and threads, which come with those objects |
| `cbAtStartOfSimTime` | done | |
| `cbReadWriteSynch` | done | |
| `cbReadOnlySynch` | done | a write, or an event scheduled before the next scheduled event, is an error |
| `cbNextSimTime` | done | never fires when no event is pending |
| `cbAfterDelay` | done | a NULL time or `vpiSuppressTime` on any simulation time reason is an error and creates no callback (38.36.2) |
| `cbAtEndOfSimTime` | done | Pre-Postponed (4.4.3.9): after the nonblocking updates, when the time slot is quiet, writes allowed (`fsim.foreign.vpi-runtime`) |
| `cbNBASynch` | done | Pre-NBA (4.4.3.3): first in the NBA region, before every nonblocking update (`fsim.foreign.vpi-runtime`) |
| `cbEndOfCompile`, `cbStartOfSimulation` | done | |
| `cbEndOfSimulation` | done | on every end of a run: `$finish`, `vpiFinish`, `--duration`, `$stop` in batch, a fatal error |
| `cbError` | done | a run-time error that ends the run (delta-cycle limit, run-time failure) runs the callbacks before `cbEndOfSimulation`; `vpi_chk_error` reports it to them (`fsim.foreign.vpi-error`) |
| `cbPLIError` | done | runs when a VPI routine fails; `vpi_chk_error` reports the error inside the callback and to the caller afterwards (`fsim.foreign.vpi-runtime`) |
| `cbSignal` | done | `index` names the signal; while a callback traps it, the signal reaches the callback at the next scheduler safe point instead of the tool's own handling (`fsim.foreign.vpi-runtime`) |
| `cbTchkViolation` | missing | gate A-G5, with the timing-check objects |
| `cbUnresolvedSystf` | done | `user_data` names the `$name`; a callback may register it with `vpi_register_systf`. It runs at `fsim elaborate --vpi` for each unknown system task or function a procedural statement calls, and elaboration then runs again with the names it registered, and at `fsim simulate` for a call site whose name the modules have not registered in that run (`fsim.foreign.vpi-unresolved`) |
| save, restart, reset, interactive reasons | missing | gate A-G6 |
| assertion callbacks (39) | partial | done: `vpi_register_assertion_cb` on a concurrent assertion with `cbAssertionSuccess`, `cbAssertionFailure`, `cbAssertionVacuousSuccess`, `cbAssertionDisabledEvaluation` (a `disable iff` ending the attempt) and the control reasons (lock, unlock, enable, disable, reset, kill, pass and fail actions), removed with `vpi_remove_cb`; the system callbacks `cbAssertionSys*` through `vpi_register_cb` (initialized and on at the start of simulation, end at its end or on `vpiAssertionSysEnd`, which removes every assertion callback); `vpi_control` assertion controls (39.5.2) and system controls (39.5.1, NULL or a scope), with lock refusing changes until unlock (`fsim.foreign.vpi-assertions`). Partial: attempt starts are not observed, so `cbAssertionStart` is refused, `attemptStartTime` is the outcome's time, `vpiAssertionKill` discards every attempt of the assertion, and a failure has no `failExpr`. Missing: step callbacks and `vpiAssertionEnableStep`/`DisableStep` (refused), immediate and deferred assertions, sequence and property instances |

## VPI object model (IEEE 1800-2023 37)

| Diagram | Clause | Status | Notes |
| --- | --- | --- | --- |
| modules, scopes, `vpiInternalScope` | 37.5, 37.10, 37.12 | partial | done: `vpiDefFile`, `vpiDefLineNo`, `vpiLibrary`, `vpiCell` (the definition without a configuration), `vpiTop`, `vpiArrayMember`, `vpiDefNetType`, `vpiUnconnDrive`, `vpiDefDelayMode` (`vpiDelayModeNone`: there are no delay-mode directives), `vpiProtected` (false: no decryption) (`fsim.foreign.vpi-model`). Missing: `vpiModule` also matches interfaces and programs; `vpiTypedef` and `vpiNetTypedef` iteration (gate A-G5) |
| mixed-language hierarchies | 37.10 | done | 1800-2023 has no VHDL objects: in a design with SystemVerilog, each VHDL entity instance is a `vpiModule` scope without objects whose `vpiDefName`, `vpiDefFile`, `vpiLibrary` and `vpiCell` name its entity, so the whole instance tree is visible and SystemVerilog instances beneath it keep their full paths (`fsim.foreign.mixed-model`); VHDL objects are reached through VHPI |
| interfaces, modports, ref objects | 37.6, 37.7, 37.15 | partial | done: `vpiModport` objects under each interface instance with `vpiInterface`, their members as `vpiIODecl` with `vpiDirection` and `vpiExpr`; an interface port's `vpiLowConn` is a `vpiRefObj` whose `vpiActual` is the interface or modport, found by name and through it (`top.u.b.data`) (`fsim.foreign.vpi-model`). Missing: `vpiInterfaceTfDecl`, clocking modport members, virtual interfaces (gate A-G5) |
| instance arrays | 37.11 | partial | done: `vpiModuleArray`, `vpiInterfaceArray`, `vpiProgramArray` beside their elements, `vpiSize`, element iteration, `vpi_handle_by_index` (`fsim.foreign.vpi-model`). `vpiRange`, `vpiLeftRange` and `vpiRightRange` from the elements' indexes. Missing: the `vpiListOp` connection expression, primitive arrays (gate A-G5) |
| ports, IODecl, high/low connections | 37.13, 37.14 | partial | done: `vpiPort` objects in port order with `vpiPortIndex`, `vpiDirection`, `vpiPortType`, `vpiSize`, `vpiConnByName`, `vpiLowConn` (the formal's net or variable), `vpiHighConn` for a simple-name actual, `vpiPorts` and `vpiPortInst` from nets and variables; a formal's net or variable has no `vpiDirection` (`fsim.foreign.vpi-model`). An interface port's low connection is a `vpiRefObj`, a modport's members are `vpiIODecl` (37.7). Missing: port bits, `vpiHighConn` expressions (gate A-G5) |
| nets, variables, var selects | 37.16–37.19 | partial | done: struct, union, enum and packed-array var/net kinds, `vpiMember`, `vpiElement` one dimension at a time, `vpiPacked`, `vpiStructUnionMember`, `vpiPackedArrayMember`, member paths in `vpi_handle_by_name`, `vpiNetArray` with `vpiNet` words, `vpiReg` only for logic vars (`fsim.foreign.vpi-model`). `vpiBit` iteration of logic and bit vectors, left to right. `vpiNetType` gives the 37.16 constants. Missing: `vpiVarSelect` (gate A-G5, with expressions); `vpiImplicitDecl` of implicit nets; string vars (gate A-G7: strings have no change hook, so they come with the direct kernel reads that replace the mirror) |
| memories, ranges | 37.20, 37.22 | done | |
| drivers and loads | 37.21 | missing | gate A-G5: a driver or load is a continuous assignment, assignment statement, port or primitive terminal, so it comes with the statement, expression and primitive objects |
| typespecs | 37.25 | partial | done: `vpiTypespec` of nets, variables, arrays, words, members and packed elements; built-in typespecs by value category, `vpiName` of the typedef with `vpiTypedefAlias` for a typedef of a named or built-in type (details 1, 6), `vpiElemTypespec` one dimension at a time and `vpiRange` through named levels (details 8-10), `vpiLeftRange`/`vpiRightRange`, `vpiArrayType` with an empty range for a variable-size dimension, `vpiTypespecMember` with `vpiTypespec`, `vpiEnumConst` with values and `vpiBaseTypespec`, `vpiSigned`, `vpiPacked`, `vpiTagged`, `vpiVector` (`fsim.foreign.vpi-model`). Partial: binding composes dimensions over a named type into the use, so the intermediate typedef names of such a chain and of an unpacked typedef's element are not kept (those levels are unnamed). Missing: `vpiIndexTypespec`, typedef iteration from a scope, parameter and class typespecs (gate A-G5) |
| nettypes, interconnects | 37.23, 37.24 | missing | gate A-G5 |
| structures and unions | 37.26 | done | unpacked: `vpiSize` is the member count, no value; packed: a vector of its bits (`fsim.foreign.vpi-model`) |
| named events, parameters | 37.27, 37.28 | partial | done: `vpiLocalParam`, built-in `vpiTypespec` by value category (`fsim.foreign.vpi-model`). Missing: parameter arrays, parameters declared in generate scopes, named typespecs of parameters |
| classes, constraints, virtual interfaces | 37.29–37.33 | missing | gate A-G5 |
| primitives, UDPs | 37.34–37.38 | missing | gate A-G5: the parser lowers gate instances to continuous assignments, so primitives need a primitive record from the frontend, published with the continuous-assignment and expression objects |
| module paths | 37.39 | done | |
| timing checks | 37.40 | missing | gate A-G5 |
| task and function declarations and calls | 37.41, 37.42 | partial | |
| processes, continuous assigns | 37.44, 37.46 | partial | done: `vpiProcess` iteration of an instance or generate scope gives `vpiInitial`, `vpiAlways` with `vpiAlwaysType` (`vpiAlways`, `vpiAlwaysComb`, `vpiAlwaysFF`, `vpiAlwaysLatch`) and `vpiFinal`; `vpiStmt` of a process, an `always` with a sensitivity list giving its `vpiEventControl`; `vpiContAssign` iteration with `vpiLhs`, `vpiRhs`, `vpiDelay`, `vpiStrength0`/`vpiStrength1`; `vpiLineNo`, `vpiColumn`, `vpiFile` (`fsim.foreign.vpi-statements`). A declaration's initializer is not a process. Missing: `vpiNetDeclAssign` assignments (the frontend folds them into the net) |
| frames, threads, delay terms, clocking | 37.43, 37.45, 37.47, 37.48 | missing | gate A-G5 |
| assertions | 37.49–37.57 | partial | done: concurrent assertions as `vpiAssert`, `vpiAssume`, `vpiCover`, `vpiRestrict`, named by their label, with `vpiFullName`, `vpiLineNo`, `vpiFile`, `vpiScope` and `vpiModule`; `vpi_iterate(vpiAssertion, NULL or instance)`; a labelled assertion is found by `vpi_handle_by_name`, an unlabelled one is not (39.3.1) (`fsim.foreign.vpi-assertions`). Missing: the property and sequence objects (`vpiProperty`, `vpiPropertyInst`, `vpiSequenceInst` and their expressions), `vpiClockingEvent`, `vpiDisableCondition`, the action statements, immediate and deferred assertions as assertion objects |
| expressions, statements | 37.58–37.79 | partial | done: `vpiStmt` iteration of named blocks and `vpiStmt` of tasks and functions; statements by kind (assignment with `vpiBlocking` and `vpiOpType`, force, release, if, if-else with `vpiElseStmt`, case with `vpiCaseType`, `vpiQualifier` and `vpiCaseItem`/`vpiExpr`, for with `vpiForInitStmt`/`vpiForIncStmt`, while, do-while, repeat, forever, delay and event controls with `vpiDelay`/`vpiCondition`, wait, event trigger, fork/begin with `vpiJoinType`, task and system task calls with `vpiArgument`, disable, break, continue, return, immediate assert, wait fork, disable fork, assign, deassign); a statement list of more than one statement is an unnamed `vpiBegin`; a named block is its scope object; expressions: `vpiConstant` with `vpiConstType`, `vpiSize`, `vpiDecompile` and its value, `vpiOperation` with `vpiOpType` and `vpiOperand`, `vpiBitSelect`, `vpiPartSelect`, `vpiIndexedPartSelect`, `vpiFuncCall`, `vpiSysFuncCall`, `vpiMethodFuncCall`; names are the objects they refer to, otherwise `vpiRefObj` (`fsim.foreign.vpi-statements`). Partial: the frontend folds output tasks, so `$display` and its kin show their format string rebuilt from the parsed conversions, a literal-only call shows its text, and the radix variants (`$displayh`, …) are named without the radix; `x++` and compound updates show as their assignment; loop-local variables are `vpiRefObj`s. Missing: `vpiVarSelect`, the parent relation of name operands beyond the scope |
| callback object | 37.80 | partial | |
| generates | 37.85 | partial | done: `vpiGenScopeArray` by name and by `vpiGenScopeArray` iteration (not a `vpiInternalScope`), `vpiSize`, `vpiGenScope` element iteration, `vpi_handle_by_index` (`fsim.foreign.vpi-model`). `vpiImplicitDecl` for blocks named by the implicit-name rule (27.6, detail 2), `vpiArrayMember` on elements, `vpiProtected`. Missing: gen var objects; genvar references as local parameters (detail 3) come with expressions (gate A-G5) |

## VHPI functions (IEEE 1076-2019 23)

| Function | Clause | Status | Notes |
| --- | --- | --- | --- |
| not-implemented functions | 17.3 | done | `vhpi_schedule_transaction`, `vhpi_create`, `vhpi_get_data`, `vhpi_put_data`, `vhpi_protected_call` raise an error until implemented |
| capability sets (`vhpiCapabilitiesP`) | 17.3 | done | claims `vhpiProvidesHierarchy` and `vhpiProvidesForeignModel` only |
| `vhpi_assert` | 23.2 | done | |
| `vhpi_check_error` | 23.3 | partial | |
| `vhpi_compare_handles` | 23.4 | done | |
| `vhpi_control` | 23.5 | partial | stop acts as finish; the argument is dropped |
| `vhpi_disable_cb` / `vhpi_enable_cb` | 23.7, 23.8 | done | enabling an after-delay callback at the end of a time step is an error |
| `vhpi_get` | 23.10 | partial | see the properties table |
| `vhpi_get_cb_info` | 23.11 | done | |
| `vhpi_get_foreignf_info` | 23.13 | done | |
| `vhpi_get_next_time` | 23.14 | done | returns Tn; `vhpiNoActivity` and TIME'HIGH when nothing remains; an error outside the simulation phase |
| `vhpi_get_phys` / `vhpi_get_real` | 23.15, 23.16 | partial / partial | `vhpi_get_real`: `vhpiFloatLeftBoundP`, `vhpiFloatRightBoundP` (the predefined REAL spans the doubles; user real types are not distinguished from integer types in the model), `vhpiRealValP` |
| `vhpi_format_value` | 23.9 | done | scalar formats (integer, enumeration, logic, character, physical and time with `unit`), `vhpiBinStrVal`/`vhpiOctStrVal`/`vhpiDecStrVal`/`vhpiHexStrVal` and the logic and bit vector formats convert among each other; other conversions are errors |
| `vhpi_get_str` | 23.17 | partial | adds `vhpiDefNameP`, `vhpiUnitNameP`, design-unit `vhpiLibLogicalNameP`, `vhpiFileNameP` and `vhpiKindStrP` for every class; `vhpiLibPhysicalNameP` missing |
| `vhpi_get_time` | 23.18 | partial | the cycle count counts only cycles with a change |
| `vhpi_get_value` | 23.19 | partial | returns the required size when the buffer is too small; `vhpiPtrVal` missing; `vhpiRawDataVal` is one byte per bit; time and physical vectors read |
| `vhpi_handle` / `vhpi_handle_by_index` / `vhpi_handle_by_name` / `vhpi_iterator` / `vhpi_scan` | 23.20–23.24, 23.31 | partial | relation gaps in gate A-G2 |
| `vhpi_is_printable` / `vhpi_printf` | 23.25, 23.27 | done | |
| `vhpi_put_value` | 23.28 | partial | signals, ports and foreign-call formals. Updating variables belongs to the advanced debug and runtime set (17.3), which fsim does not claim; process variables need the process statement regions, so both come with gate A-G5. Drivers: gate A-G5 |
| `vhpi_register_cb` | 23.29 | partial | reasons listed below |
| `vhpi_register_foreignf` | 23.30 | partial | architecture and library foreign models are registered but never run |
| `vhpi_release_handle` | 23.31 | done | releasing a callback handle neither removes it nor changes its state |
| `vhpi_remove_cb` | 23.32 | done | the callback leaves the information model and its handles become invalid |

## VHPI value formats and put modes (IEEE 1076-2019 22)

| Item | Clause | Status | Notes |
| --- | --- | --- | --- |
| native formats and `vhpiObjTypeVal` | 22.4 | done | |
| `vhpiBinStrVal` / `vhpiOctStrVal` / `vhpiDecStrVal` / `vhpiHexStrVal` on scalars | 22.2.8 | done | the position number in that radix |
| `vhpiBinStrVal` on STD_ULOGIC | 22.2.8, 22.4 | choice | reads as the logic character ('U', 'X', '0', '1', ...), as for logic arrays; the format is optional (22.4), and VHPI programs such as cocotb compare it against "0" and "1" |
| `vhpiOctStrVal` / `vhpiDecStrVal` / `vhpiHexStrVal` on vectors | 22.2.8 | done | BIT and STD_ULOGIC vectors: right-aligned digit groups; an unknown bit group reads as its common character or X; decimal needs known bits and at most 64 of them |
| `vhpiRawDataVal` | 22.4 | done | implementation-defined: one byte per bit, least significant first, holding the logic code |
| `vhpiTimeVecVal` / `vhpiPhysVecVal` | 22.2.8 | done | one-dimensional arrays of time or physical values |
| `vhpiDeposit` | 22.5.3 | done | immediate, without propagation; not a value-change trigger (21.3.2.2 NOTE 1) |
| `vhpiDepositPropagate` | 22.5.3 | done | applied in the next signal-update phase |
| `vhpiForce` | 22.5.3 | done | immediate; not a value-change trigger |
| `vhpiForcePropagate` | 22.5.3 | done | applied in the next signal-update phase; a force wins over a deposit in the same update |
| `vhpiRelease` | 22.5.3 | done | |
| `vhpiSizeConstraint` | 22.5 | missing | |
| updates at the end of a time step / simulation | 21.3.6.8, 21.3.7.6 | done | errors |

## VHPI callback reasons (IEEE 1076-2019 21.3)

| Reason | Status | Notes |
| --- | --- | --- |
| `vhpiCbValueChange` | partial | signals only; changes coalesce at the next safe point |
| `vhpiCbForce`, `vhpiCbRelease` | done | repetitive; on a signal or a name of its elements, or NULL for every signal. Triggered by a force or release assignment, which the callbacks see after the update that applies it, with the resulting value, and by `vhpi_put_value` with `vhpiForce`/`vhpiRelease` (at once) or `vhpiForcePropagate` (after its update). A callback on an element runs when the target contains it (21.3.2.3); a NULL callback gets the declared signal as `obj` (`fsim.foreign.vhpi-runtime`) |
| `vhpiCbTransaction` | partial | repetitive; once per cycle in which the signal is active, with or without an event (14.7.3.1) (`fsim.foreign.vhpi-runtime`). On a name of an element it runs when the signal is active. Driver objects come with gate A-G5 |
| `vhpiCbStmt`, `vhpiCbResume`, `vhpiCbSuspend`, `vhpiCbSubpCall`, `vhpiCbSubpReturn`, `vhpiCbSensitivity` | missing | gate A-G5: their trigger objects are process, statement and subprogram objects |
| `vhpiCbAfterDelay` / `vhpiCbRepAfterDelay` | done | registering one at the end of a time step is an error |
| `vhpiCbTimeOut` / `vhpiCbRepTimeOut` | done | after the cycle's signal updates and `vhpiCbStartOfProcesses`, with the processes resumed by a timeout (14.7.5.3 f 3); `obj` and `value` are ignored (21.3.3.2); an end-of-time-step callback cannot register or enable one |
| `vhpiCbNextTimeStep`, `vhpiCbStartOfNextCycle`, `vhpiCbEndOfProcesses`, `vhpiCbLastKnownDeltaCycle`, `vhpiCbEndOfTimeStep` (and repetitive forms) | done | a first cycle at time zero is a delta cycle (14.7.5.1): it runs StartOfNextCycle but not NextTimeStep; EndOfProcesses also runs at initialization step h); the first cycle of a time step runs NextTimeStep, StartOfNextCycle and the after-delay callbacks before its signal updates; EndOfTimeStep runs after the postponed processes (h 6) |
| `vhpiCbStartOfProcesses`, `vhpiCbStartOfPostponed` (and repetitive forms) | done | initialization steps e) and i), cycle steps f 1) and h 3); from `vhpiCbStartOfPostponed` to the end of the time step an update is an error (14.7.5.3 h) |
| `vhpiCbStartOfTool` | done | immediately before the registration phase completes |
| `vhpiCbEndOfTool` | done | in the termination phase, also after an error ends the run |
| `vhpiCbStartOfAnalysis` / `vhpiCbEndOfAnalysis` | N-A | `fsim simulate` has no analysis phase (21.3.7.3) |
| `vhpiCbStartOfElaboration` / `vhpiCbEndOfElaboration` | N-A | `fsim simulate` has no elaboration phase (21.3.7.4) |
| `vhpiCbStartOfInitialization` | done | before any process runs (14.7.5.2 a) |
| `vhpiCbEndOfInitialization` | done | after the processes' first run (14.7.5.2 m) |
| `vhpiCbStartOfSimulation` | done | before the first simulation cycle |
| `vhpiCbEndOfSimulation` | done | on every end of a run; updates inside it are errors |
| `vhpiCbQuiescense` | done | after the end of a time step with nothing left to simulate (14.7.5.3 h 7); it may cause further cycles (`fsim.foreign.vhpi-runtime`) |
| `vhpiCbPLIError` | done | Annex B reason, repetitive: runs as a VHPI routine that raised an error returns; `vhpi_check_error` reports the error inside the callback and to the caller afterwards (`fsim.foreign.vhpi-runtime`) |
| `vhpiCbSigInterrupt` | done | fsim's interrupt event is SIGINT: while a callback is registered, an interrupt reaches it at the next scheduler safe point and the run goes on (`fsim.foreign.vhpi-runtime`) |
| interactive, save, restart, reset reasons | missing | gate A-G6 |

## VHPI information model (IEEE 1076-2019 19)

| Item | Status | Notes |
| --- | --- | --- |
| `rootInst`, `compInstStmt`, block and for/if generates, `packInst`, `tool`, `argv` | done | `vhpiRootInst` is the first VHDL root, or in a design without one the first root; other roots resolve by name |
| mixed-language hierarchies | done | 1076-2019 leaves them to the tool: a SystemVerilog instance is a `compInstStmt` (or the root) region without declarations, so VHDL instances beneath it are reachable and full paths through it resolve (`fsim.foreign.mixed-model`) |
| `aliasDecl` | done | `vhpiAliasDecls` and `vhpiDecls` list aliases of objects; the value, indexes and sub-names read through the target; `vhpiAliasedName` |
| subprogram, component, attribute, file and group declarations | done | `vhpiFuncDeclK`, `vhpiProcDeclK` (one object per overload), `vhpiCompDeclK`, `vhpiAttrDeclK`, `vhpiAttrSpecK`, `vhpiFileDeclK`, `vhpiGroupTempDeclK`, `vhpiGroupDeclK` in `vhpiDecls` (`fsim.foreign.vhpi-model`); their parameters, ports and bodies come with the static access set (gate A-G5) |
| case generates | choice | the case generate statement has no class in Annex B of the header, so a selected alternative is an `ifGenerate` |
| `sliceName` | done | `vhpi_handle_by_name` with `name(left downto right)` or `(left to right)` of one-dimensional arrays; null or opposite-direction slices are not found |
| processes and statements | partial | done: `vhpiStmts` of a root, component instance, block or generate region gives its `processStmt` objects and the concurrent statements equivalent to processes (`simpleSigAssignStmt`, `condSigAssignStmt`, `selectSigAssignStmt`, `concAssertStmt`, `concProcCallStmt`; the entity's statements first), then its component instantiations, blocks and generates. An unlabeled one has the implicit label `_P<n>` (19.4.2.3); a loop `_L<n>` (19.4.2.2). `vhpiSensitivitys`, `vhpiDecls`, `vhpiVarDecls`, `vhpiConstDecls` and `vhpiSeqStmts` of a process; sequential statements by class (`seqSigAssignStmt`, `varAssignStmt`, `ifStmt` and `caseStmt` with `vhpiBranchs` and `vhpiChoices`, `forLoop` with `vhpiParamDecl`, `whileLoop`, `foreverLoop`, `exitStmt`, `nextStmt`, `returnStmt`, `seqProcCallStmt`, `seqAssertStmt`, `reportStmt`, `waitStmt` with `vhpiSigNames`, `nullStmt`), nesting through `vhpiSeqStmts` of loops and branches; `vhpiCondWaveforms`, `vhpiSelectWaveforms` and `vhpiWaveformElems` of signal assignments. Properties `LabelName`, `LoopLabelName`, `IsSeqStmt`, `IsPostponed`, `IsPassive` (a process with a signal assignment or procedure call is not passive), `IsForeign`, `IsGuarded`, `IsTransport`, `IsUnaffected`, `LineNo`, `FileName`. Expression relations (`vhpiCondExpr`, `vhpiLhsExpr`, `vhpiRhsExpr`, `vhpiCaseExpr`, `vhpiSelectExpr`, `vhpiReturnExpr`, `vhpiTimeOutExpr`, `vhpiReportExpr`, `vhpiSeverityExpr`, `vhpiRejectTime`, `vhpiTimeExpr`, `vhpiValExpr`) lead to a name that denotes a declared object (or an element of one with literal indexes) or to an integer literal; any other expression has no object, so the relation is empty (`fsim.foreign.vhpi-statements`). A process is also found by `vhpi_handle_by_name`. Missing: the other expression classes (operators, function calls, aggregates, other literals), `vhpiConstraint` of a for loop, `vhpiParamAssocs` of procedure calls, the equivalent process of a concurrent statement (`vhpiEqProcessStmt`) and its implicit `vhpiSensitivitys`, `vhpiBeginLineNoP`/`vhpiEndLineNoP`, `vhpiLineOffsetP`, force and release statements and sequential blocks (no class), statements inside subprogram bodies, `vhpi_create` of processes. An `elsif` and an `else` holding only one unlabeled `if` statement are not told apart; `loop` and `while true loop` are both a `foreverLoop`; a process variable's value is not readable |
| drivers, contributors, loads, simNets, basic signals | missing | gate A-G5: drivers belong to process statements (and to `vhpi_create`d drivers), so they come with the process and statement classes; `vhpi_schedule_transaction` acts on them |
| selected and indexed names | done | |
| properties `Name`, `CaseName`, `FullName`, `FullCaseName`, `KindStr`, `Kind`, `Size`, `NumDimensions`, `IsScalar`, `IsComposite`, `IsUnconstrained`, `ResolutionLimit`, `Argc`, `Position`, `Mode`, `GenerateIndex`, `IsNull`, `IsDiscrete`, `IsGuarded`, `BaseIndex` | done | |
| properties `LeftBound`, `RightBound`, `IsUp` | done | every range object (`vhpiConstraints` gives one per dimension; int, enum, phys and float range classes follow the index type), scalar types, and the first dimension of an array object |
| property `ToolVersion` | partial | the fsim release version |
| properties `Staticness`, `SigKind`, `IsForced`, `NumLiterals` | done | `Staticness`: ranges and literals locally static, generics and constants without literal value globally static, signals and ports globally static (static names), variables dynamic, names as their prefix; `IsForced` is the whole signal's force state |
| properties `DefName`, `UnitName` | done | `@unit{.scope}.name` for instances, generates, objects and predefined types; user-declared types have none |
| properties `FileName`, `LineNo` | done | declarations, component instantiation, block and generate statements, design units; the root instance and package instances take their design unit's position (`fsim.foreign.vhpi-model`) |
| properties `LineOffset`, `ForeignKind` | missing | `LineOffset` belongs to the advanced debug and runtime set (17.3); `ForeignKind` comes with foreign architectures (gate A-G5) |
| relations `vhpiEntityDecl`, `vhpiCurRegion`, `vhpiImmRegion`, `vhpiConstraint`, `vhpiAliasedName`, and the deprecated `vhpiSubtype`, `vhpiElemSubtype`, `vhpiValSubtype`, `vhpiDecl` | done | the deprecated ones are the same as `vhpiType`, `vhpiElemType`, `vhpiValType` and the declared object of a name |
| extended identifiers in `vhpi_handle_by_name` | done | compared case sensitively; basic identifiers stay case insensitive (15.4.2, 15.4.3) |
