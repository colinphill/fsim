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

Registration is available only to Verilog and SystemVerilog profiles. Exact
name and callable kind are significant. VHDL does not inherit the TF
namespace.

## ACC behavior

ACC handles are generation-qualified views of the common hierarchy and VPI
objects. The standard surface supports scoped/name lookup, object traversal,
typed reads, deposit/force/release and scheduled updates, timing and path
objects, value-change links, callbacks, and shared TF/ACC work areas. ACC and
VPI handles for the same object resolve to the same simulation identity.

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
