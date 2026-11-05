<!-- SPDX-License-Identifier: Apache-2.0 -->
# IEEE TF and ACC PLI

fsim v3 supplies independently authored, standard-compatible TF and ACC
headers and a direct-v3 native plug-in ABI. Vendor-only registration names and
extensions are rejected; the implementation does not emulate a proprietary
simulator ABI.

## SDK surfaces

Installed consumers include:

- `fsim/runtime/veriuser.h` for TF declarations and constants;
- `fsim/runtime/acc_user.h` for ACC declarations and constants;
- `fsim/runtime/tf_plugin_abi.h` for bounded direct-v3 plug-in metadata;
- `fsim_tf`, as a Linux shared library or Windows import library.

C consumers use C11 and C++ consumers use C++20. A plug-in descriptor declares
its ABI version, structure size, name, and registration table. fsim copies and
validates metadata before publishing any registration. v2 plug-ins are
rejected directly and have no compatibility reader.

## TF behavior

The host implements `checktf`, `sizetf`, `calltf`, and `misctf`; typed integer,
real, string, vector, expression, parameter, instance, time, delay, timescale,
scope, work-area, and user-data access; output and simulation controls; and
read-write/read-only synchronization. Calls and callbacks pass through one
scheduler coordinator so deterministic region and stable-order rules apply to
foreign code as well as HDL.

`tf_asynchon` enables `misctf(reason_paramvc)` for each change of a task or
function argument, and the call keeps a current and a saved parameter value
change flag per argument (`tf_copypvc_flag`, `tf_movepvc_flag`,
`tf_testpvc_flag`, `tf_getpchange`, 25.9 to 25.59). No value change callback
exists for an instance until it calls `tf_asynchon`, so unused flags cost
nothing. `tf_strdelputp`, `tf_strlongdelputp` and `tf_strrealdelputp` schedule
a string value on an argument after a delay in the module's time units, by
inertial, modified transport or pure transport delay (25.52 to 25.56).

The TF routines follow IEEE 1364-2001 clauses 24 and 25 and Annex F; the
routine-by-routine status is in the TF section of
[foreign-interface conformance](foreign-interface-conformance.md). Routines
that clause 25 does not define are not supplied: `tf_gettflist`,
`tf_getlongsimtime`, `tf_getroutine`, `tf_igetroutine`, `tf_specialparam` and
`err_intercept`.

A PLI 1.0 library loaded with `--vpi` associates its applications with names
through the `veriusertfs[]` table it exports (21.2 leaves the mechanism to the
vendor), which fsim registers as the library loads; `veriusertfs_register_table`
does the same from a startup routine. The `endofcompile_routines[]` array of
Annex F runs once after elaboration and before time 0. A `misctf` routine is
called for `reason_endofcompile`, `reason_paramvc`, `reason_synch`,
`reason_rosynch`, `reason_reactivate`, `reason_finish`, `reason_force`,
`reason_release`, `reason_disable` and `reason_paramdrc` (a change on a
primitive driver of a net argument, after `tf_asynchon`); the reasons that
belong to `$reset`, `$save`, `$restart`, `$stop` and `$scope` are not called
yet. `reason_disable` needs `simulate --foreign-probes`. The test is
`fsim.foreign.pli-mechanism`.

Registration is available only to Verilog and SystemVerilog profiles. Exact
name and callable kind are significant. VHDL does not inherit the TF
namespace.

## ACC behavior

ACC handles are generation-qualified views of the common hierarchy and VPI
objects. A task or function registered from a `veriusertfs` table gets the
ACC routines over the VPI: scoped and name lookup, traversal, typed reads,
deposit, force and release, value-change links, callbacks, and shared TF/ACC
work areas. ACC and VPI handles for the same object resolve to the same
simulation identity. The delay and pulse-limit routines
(`acc_fetch_delays`, `acc_append_delays`, `acc_replace_delays`, the pulse
routines and `acc_fetch_delay_mode`) work on primitives, module paths and
timing checks through `vpi_get_delays` and `vpi_put_delays`, honouring
`accMinTypMaxDelays`, `accPathDelayCount` and `accToHiZDelay`; a change on a
primitive retimes its later output transitions. Module input ports,
intermodule path delays and `accMapToMipd` are not available, and
`acc_handle_datapath` finds nothing; `acc_handle_path` finds the intermodule
path that a delay annotation created; the
[conformance table](foreign-interface-conformance.md#acc-routines-ieee-1364-2001-22-23)
lists each routine's status.

## Lifetime and failure containment

The loader validates readable, writable, and executable native ranges before
dereferencing plug-in data or callbacks. Exceptions, invalid pointers,
re-entry, duplicate registrations, stale handles, unload, and callback
cancellation fail transactionally. A bound call keeps its library image alive
until the final lease is released. Foreign calls are serialized at scheduler
safe points, which is also the defined behavior for later parallel execution.

The complete VPI interface is described in the
[SystemVerilog VPI guide](systemverilog-vpi.md); stable C ABI rules are listed
in the [v3 API reference](v3-api.md).
