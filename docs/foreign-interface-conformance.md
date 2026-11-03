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
| `vpi_get_str` | 38.11 | partial | `vpiDefFile`, `vpiLibrary`, `vpiCell`, `vpiConfig` missing |
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
| `cbStmt`, `cbForce`, `cbRelease`, `cbAssign`, `cbDeassign`, `cbDisable` | missing | gate A-G4 |
| `cbCreateObj`, `cbReclaimObj`, `cbEndOfObject`, `cbSizeChange`, `cbStartOfFrame`, `cbEndOfFrame`, `cbStartOfThread`, `cbEndOfThread`, `cbEnterThread` | missing | gate A-G4 |
| `cbAtStartOfSimTime` | done | |
| `cbReadWriteSynch` | done | |
| `cbReadOnlySynch` | done | a write, or an event scheduled before the next scheduled event, is an error |
| `cbNextSimTime` | done | never fires when no event is pending |
| `cbAfterDelay` | partial | no error for `vpiSuppressTime` |
| `cbAtEndOfSimTime` | partial | runs in the reactive region |
| `cbNBASynch` | missing | gate A-G4 |
| `cbEndOfCompile`, `cbStartOfSimulation` | done | |
| `cbEndOfSimulation` | done | on every end of a run: `$finish`, `vpiFinish`, `--duration`, `$stop` in batch, a fatal error |
| `cbError`, `cbPLIError`, `cbTchkViolation`, `cbSignal`, `cbUnresolvedSystf` | missing | gate A-G4 |
| save, restart, reset, interactive reasons | missing | gate A-G6 |
| assertion callbacks (39) | missing | gate A-G4 |

## VPI object model (IEEE 1800-2023 37)

| Diagram | Clause | Status | Notes |
| --- | --- | --- | --- |
| modules, scopes, `vpiInternalScope` | 37.5, 37.12 | partial | `vpiModule` also matches interfaces and programs; module properties `vpiDefFile`, `vpiTop`, `vpiLibrary`, `vpiCell` missing |
| interfaces, modports | 37.6, 37.7 | partial / missing | |
| instance arrays | 37.11 | missing | gate A-G2 |
| ports, IODecl, high/low connections | 37.13, 37.14 | partial | ports are typed as nets or regs |
| nets, variables, var selects | 37.16–37.19 | partial | struct, union, enum and packed-array kinds; `vpiNetArray`; `vpiBit` iteration; `vpiVarSelect` |
| memories, ranges | 37.20, 37.22 | done | |
| drivers and loads | 37.21 | missing | gate A-G2 |
| typespecs, nettypes, interconnects | 37.23–37.26 | missing | gate A-G2 |
| named events, parameters | 37.27, 37.28 | partial | arrays, `vpiLocalParam`, typespec missing |
| classes, constraints, virtual interfaces | 37.29–37.33 | missing | gate A-G5 |
| primitives, UDPs | 37.34–37.38 | missing | gate A-G2 |
| module paths | 37.39 | done | |
| timing checks | 37.40 | missing | gate A-G5 |
| task and function declarations and calls | 37.41, 37.42 | partial | |
| frames, threads, delay terms, continuous assigns, clocking | 37.43–37.48 | missing | gate A-G5 |
| assertions | 37.49–37.57 | missing | gate A-G5 |
| expressions, statements | 37.58–37.79 | missing | gate A-G5 |
| callback object | 37.80 | partial | |
| generates | 37.85 | partial | `vpiGenScopeArray` and `vpiGenVar` missing |

## VHPI functions (IEEE 1076-2019 23)

| Function | Clause | Status | Notes |
| --- | --- | --- | --- |
| not-implemented functions | 17.3 | done | `vhpi_schedule_transaction`, `vhpi_format_value`, `vhpi_create`, `vhpi_get_data`, `vhpi_put_data`, `vhpi_protected_call` raise an error until implemented |
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
| `vhpi_get_phys` / `vhpi_get_real` | 23.15, 23.16 | partial / missing | |
| `vhpi_get_str` | 23.17 | partial | |
| `vhpi_get_time` | 23.18 | partial | the cycle count counts only cycles with a change |
| `vhpi_get_value` | 23.19 | partial | returns the required size when the buffer is too small; `vhpiRawDataVal`, `vhpiPtrVal`, time and physical vectors missing |
| `vhpi_handle` / `vhpi_handle_by_index` / `vhpi_handle_by_name` / `vhpi_iterator` / `vhpi_scan` | 23.20–23.24, 23.31 | partial | relation gaps in gate A-G2 |
| `vhpi_is_printable` / `vhpi_printf` | 23.25, 23.27 | done | |
| `vhpi_put_value` | 23.28 | partial | signals only; variables and drivers in gate A-G2 |
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
| `vhpiRawDataVal` | 22.4 | missing | required by 22.4; gate A-G2 |
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
| `vhpiCbForce`, `vhpiCbRelease`, `vhpiCbTransaction`, `vhpiCbStmt`, `vhpiCbResume`, `vhpiCbSuspend`, `vhpiCbSubpCall`, `vhpiCbSubpReturn`, `vhpiCbSensitivity` | missing | gate A-G4 |
| `vhpiCbAfterDelay` / `vhpiCbRepAfterDelay` | done | registering one at the end of a time step is an error |
| `vhpiCbTimeOut` / `vhpiCbRepTimeOut` | missing | gate A-G4 |
| `vhpiCbNextTimeStep`, `vhpiCbStartOfNextCycle`, `vhpiCbEndOfProcesses`, `vhpiCbLastKnownDeltaCycle`, `vhpiCbEndOfTimeStep` (and repetitive forms) | done | a first cycle at time zero is a delta cycle (14.7.5.1): it runs StartOfNextCycle but not NextTimeStep; EndOfProcesses also runs at initialization step h) |
| `vhpiCbStartOfProcesses`, `vhpiCbStartOfPostponed` (and repetitive forms) | missing | gate A-G4 |
| `vhpiCbStartOfTool` | done | immediately before the registration phase completes |
| `vhpiCbEndOfTool` | done | in the termination phase, also after an error ends the run |
| `vhpiCbStartOfAnalysis` / `vhpiCbEndOfAnalysis` | N-A | `fsim simulate` has no analysis phase (21.3.7.3) |
| `vhpiCbStartOfElaboration` / `vhpiCbEndOfElaboration` | N-A | `fsim simulate` has no elaboration phase (21.3.7.4) |
| `vhpiCbStartOfInitialization` | done | before any process runs (14.7.5.2 a) |
| `vhpiCbEndOfInitialization` | done | after the processes' first run (14.7.5.2 m) |
| `vhpiCbStartOfSimulation` | done | before the first simulation cycle |
| `vhpiCbEndOfSimulation` | done | on every end of a run; updates inside it are errors |
| `vhpiCbQuiescense`, `vhpiCbPLIError`, `vhpiCbSigInterrupt` | missing | gate A-G4 |
| interactive, save, restart, reset reasons | missing | gate A-G6 |

## VHPI information model (IEEE 1076-2019 19)

| Item | Status | Notes |
| --- | --- | --- |
| `rootInst`, `compInstStmt`, block and for/if generates, `packInst`, `tool`, `argv` | done | the first VHDL root only; Verilog children are empty regions (gate A-G3) |
| case generates, aliases, subprogram, component, attribute, file and group declarations | missing | gate A-G2 |
| processes and statements | missing | gate A-G5 |
| drivers, contributors, loads, simNets, basic signals | missing | gate A-G2 |
| selected and indexed names | done | slice names missing |
| properties `Name`, `CaseName`, `FullName`, `FullCaseName`, `KindStr`, `Kind`, `Size`, `NumDimensions`, `IsScalar`, `IsComposite`, `IsUnconstrained`, `ResolutionLimit`, `Argc`, `Position`, `Mode`, `GenerateIndex`, `IsNull`, `IsDiscrete`, `IsGuarded`, `BaseIndex` | done | |
| properties `LeftBound`, `RightBound`, `IsUp`, `ToolVersion` | partial | first dimension only; `IsUp` on integer ranges only; `ToolVersion` is the fsim release version |
| properties `Staticness`, `FileName`, `LineNo`, `LineOffset`, `DefName`, `UnitName`, `SigKind`, `IsForced`, `NumLiterals`, `ForeignKind` | missing | gate A-G2 |
