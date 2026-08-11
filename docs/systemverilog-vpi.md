<!-- SPDX-License-Identifier: Apache-2.0 -->
# SystemVerilog VPI support

fsim v2 provides a simulation-owned, versioned VPI boundary for
Verilog-2005 and SystemVerilog-2017. The implementation uses stable integer
identities and owning C++ services; neither a plug-in nor an artifact receives
addresses of simulator objects.

## ABI and plug-in lifecycle

The public C header is
`include/fsim/runtime/vpi_abi.h`. The original 40-byte v1 host is frozen and
supports bounded diagnostic reporting. The 56-byte v2 host begins with that
complete v1 table and appends a service context plus one C-compatible invocation
callback. Every table and request carries an explicit size. A plug-in must
validate `abi_version` and `struct_size` before reading a field.

Plug-ins export exactly `fsim_vpi_plugin_bind_v1` using `FSIM_VPI_EXPORT`
and `FSIM_VPI_CALL`. Loading validates the host, opens the image, resolves
the exact symbol, contains bind/startup exceptions, validates the returned
descriptor, and publishes one move-only owner only after successful startup.
Explicit shutdown and destruction invoke plug-in shutdown exactly once before
the image unloads. C and C++ reference images exercise the same contract.

A minimal service image includes only the public C header, validates the v2
prefix, invokes services through the sized request/result records, and returns
a sized descriptor from the exact bind symbol:

```c
#include "fsim/runtime/vpi_abi.h"

static fsim_vpi_status_v1 FSIM_VPI_CALL lifecycle(void *context) {
  (void)context;
  return FSIM_VPI_STATUS_OK;
}

FSIM_VPI_EXPORT fsim_vpi_status_v1 FSIM_VPI_CALL
fsim_vpi_plugin_bind_v1(const fsim_vpi_host_v1 *host,
                        fsim_vpi_plugin_v1 *plugin) {
  static const char name[] = "example-vpi";
  if (!host || !plugin || host->abi_version != FSIM_VPI_HOST_ABI_VERSION_V2 ||
      host->struct_size < sizeof(fsim_vpi_host_v2))
    return FSIM_VPI_STATUS_UNSUPPORTED;
  plugin->abi_version = FSIM_VPI_PLUGIN_ABI_VERSION;
  plugin->struct_size = sizeof(*plugin);
  plugin->flags = 0;
  plugin->name_size = sizeof(name) - 1;
  plugin->name = name;
  plugin->context = 0;
  plugin->startup = lifecycle;
  plugin->shutdown = lifecycle;
  return FSIM_VPI_STATUS_OK;
}
```

The buildable C and C++ examples in `tests/runtime/vpi_reference_plugin_c.c`
and `tests/runtime/vpi_reference_plugin_cpp.cpp` demonstrate all ten service
families and exact startup/shutdown handling.

## Object and value model

Object handles encode simulation registry, slot, and generation. Roots,
modules, interfaces, programs, packages, generate scopes, ports, nets,
variables, parameters, memories, arrays, classes/properties, and named events
retain parent, canonical full name, optional source, and exact type metadata.
Lookup, child lookup, child iteration, scan, release, stale reuse, and
cross-simulation use return distinct errors.

Scalar and recursive type descriptors cover packed/unpacked arrays, dynamic
arrays, queues, associative arrays, structs, unions, enums, strings, classes,
and class handles within documented resource limits. Checked values cover
two-, four-, and nine-state vectors, integers, real/shortreal, strings, time,
and strengths. Deposits update the underlying value; force is a separate
visible layer; deposits continue beneath force; release reveals the latest
underlying value. Delayed deposit/force/release use the common scheduler.

## Time, callbacks, control, and system callables

Time queries preserve 64-bit ticks, scaled-real conversion, time unit,
precision, and delta. Value-change, delay, read-write, read-only, next-time,
synchronization, simulation, reset, save, and restart callbacks use common
scheduler regions and copied event data. Removal, self-removal, nested
registration, re-entry, user exceptions, and teardown are contained.

Stop, interactive, reset, finish, force, and release operations execute at
defined safe points. System tasks/functions have validated profiles,
compile/size/call phases, registration/call/argument handles, separate
registration and call user data, typed result publication, retained call
records, re-entry, sealing, unregister, release, and deterministic teardown.

## I/O and diagnostics

Portable descriptors use bit zero for standard output, bits 1-30 for MCD
channels, and the high bit for file descriptors independently of native
descriptor width. Paths are bounded root-relative UTF-8 names. Vlog,
note/warning/error/fatal diagnostics, bounded `{}` formatting, argv,
product/version, MCD fan-out, flush, close, append, and sink exception
containment are simulation-owned.

## Save, restart, and artifacts

Same-process restart retains live plug-in owners, callbacks, registrations,
user data, exact handles, retained calls, descriptors, and forced values. Its
checkpoint requires unchanged simulation, external-owner inventory, content/
cache identity, and ordered plug-in provenance.

Portable artifacts retain complete typed object value/force state and ordered
plug-in path, name, content, and host fingerprints. Restore validates schema,
host and plug-in ABI, content, cache, provenance, complete object inventory,
deep type identity, and every value before mutation. Object handles remap by
canonical full name. Native closures, pointer-valued user data, system
registrations/calls, open streams, and dynamic-library contexts are returned as
explicit counted invalidations and must be recreated after verified plug-in
reload.

## Portability and limits

The public boundary uses fixed-width integers, explicit byte counts, stable
calling/export macros, and no C++ types. The C and C++ reference images build
independently on the platform shared-library path. Repeated and relocated loads
must produce the same ordered service transcript.

VPI is a governed compatibility surface, not a promise that arbitrary
third-party simulator internals or proprietary extensions are emulated.
Nonrestorable native state is never serialized. Unsupported profiles, invalid
encodings, oversize inputs, foreign/stale handles, and resource exhaustion
return checked failures without partial publication.

Packed Verilog/SystemVerilog values are not capped at one megabit or at a host
word. Type descriptors and stored `aval`/`bval` planes preserve the exact
declared width, signedness, and `X`/`Z` state, and application publication keeps
that identity across interpreter/LLVM execution, callbacks, checkpoints, and
relocation. The public descriptor width is explicitly `uint32_t`; exceeding
that host ABI representation is a diagnosed physical boundary, distinct from
Verilog language legality. Configured allocation/work budgets are likewise
transactional resource limits and never justify narrowing a value.

Primary evidence lives in `tests/runtime/runtime_vpi_*_tests.cpp`,
`tests/runtime/vpi_reference_plugin_c.c`, and
`tests/runtime/vpi_reference_plugin_cpp.cpp`.
