<!-- SPDX-License-Identifier: Apache-2.0 -->
# VHDL VHPI support

For occurrence-qualified VHDL/embedded-PSL debugger state, artifact replay,
and portable handle remapping, also see the public
[`vhdl-psl.md`](vhdl-psl.md) support boundary and
[`vhdl-psl-tutorial.md`](vhdl-psl-tutorial.md).

fsim v2 provides a simulation-owned, versioned VHPI boundary for VHDL-87,
VHDL-93, VHDL-2000, VHDL-2002 and VHDL-2008.
The implementation uses stable integer identities and owning C++ services;
plug-ins and portable artifacts never receive addresses of simulator objects.
VHPI handles, regions, selected names, values, and restart rules are distinct
from the SystemVerilog VPI boundary.

Each visible older-mode scope reports its canonical year, revision-specific
predefined-environment identity, compiler compatibility profile and exact
selected package revision/digest records. The same fields survive standalone
design load, relocation, checkpoint remapping and replay. Compiler-owned
`ieee.std_logic_*` implementation units are deliberately absent from hierarchy
enumeration; user occurrences expose only their dependency provenance.

## ABI and plug-in lifecycle

The public C header is `include/fsim/runtime/vhpi_abi.h`. The frozen 40-byte
v1 host supports bounded diagnostic reporting. The 56-byte v2 host begins with
that complete v1 table and appends a service context plus one C-compatible
invocation callback. Every table and service record has an explicit size.
Images must check both `abi_version` and `struct_size` before reading an
extension.

Images export exactly `fsim_vhpi_plugin_bind_v1` using `FSIM_VHPI_EXPORT`
and `FSIM_VHPI_CALL`. The loader validates the host, opens the image, resolves
that exact symbol, contains bind/startup exceptions, validates and copies the
descriptor, and publishes one move-only owner only after successful startup.
Explicit shutdown and destruction invoke shutdown exactly once before unload.

A minimal service image includes only the public C header:

```c
#include "fsim/runtime/vhpi_abi.h"

static fsim_vhpi_status_v1 FSIM_VHPI_CALL lifecycle(void *context) {
  (void)context;
  return FSIM_VHPI_STATUS_OK;
}

FSIM_VHPI_EXPORT fsim_vhpi_status_v1 FSIM_VHPI_CALL
fsim_vhpi_plugin_bind_v1(const fsim_vhpi_host_v1 *host,
                         fsim_vhpi_plugin_v1 *plugin) {
  static const char name[] = "example-vhpi";
  if (!host || !plugin ||
      host->abi_version != FSIM_VHPI_HOST_ABI_VERSION_V2 ||
      host->struct_size < sizeof(fsim_vhpi_host_v2))
    return FSIM_VHPI_STATUS_UNSUPPORTED;
  plugin->abi_version = FSIM_VHPI_PLUGIN_ABI_VERSION;
  plugin->struct_size = sizeof(*plugin);
  plugin->flags = 0;
  plugin->name_size = sizeof(name) - 1;
  plugin->name = name;
  plugin->context = 0;
  plugin->startup = lifecycle;
  plugin->shutdown = lifecycle;
  return FSIM_VHPI_STATUS_OK;
}
```

The buildable C and C++ images in
`tests/runtime/vhpi_reference_plugin_c.c` and
`tests/runtime/vhpi_reference_plugin_cpp.cpp` exercise all thirteen service
families, diagnostic reporting, and exact startup/shutdown.

## Hierarchy, types, and values

Object and iterator handles encode a VHPI-only tag, registry, generation, and
slot. Named roots and descendants own canonical case-insensitive basic names,
case-exact extended names, signed multidimensional indices, selected/full
names, source locations, parent/kind metadata, and stable creation order.
Lookup and relationship iteration distinguish invalid, stale, released,
exhausted, and cross-simulation identities.

Type/subtype descriptors retain scalar kind, canonical base identity,
resolution function, direction, null ranges, and bounded recursive array/
record constraints. Values cover Boolean, bit, Unicode character, integer,
finite real, time, enumeration position, physical magnitude/unit, access/null,
multidimensional arrays, records, files, protected objects, resolved values,
and all nine standard-logic states. Caller buffers report exact required sizes
and remain untouched when short. No native layout is part of a value.

## Drivers, time, callbacks, and foreign calls

Signal drivers and sources retain projected transactions with exact waveform,
inertial/transport and rejection policy. Deposit, force, release, and
transaction cancellation use checked owner/type identities; force layers mask
continuing driver activity without discarding it.

Time queries retain ticks, unit, precision, delta, phase, and next time.
Callbacks cover signal, process, event, transaction, assertion, simulation,
save, restart, and reset lifecycle regions with copied data, safe self/peer
removal, nested registration/re-entry, exception containment, and deterministic
teardown.

Foreign subprograms and models retain copied profiles, typed arguments/results,
call and registration user data, lifecycle, nested calls, and checked release.
Generic/port associations retain exact formal/actual/mode/class metadata,
explicit open/disconnected actuals, owner/root identity, and independent object
and call user data.

## Reporting and multiple roots

Assertion, report, and raw-output contexts bind to one exact root. Multiple
contexts may share a root, while different roots remain isolated. Events own
severity, source, message, subject, and a global ordinal before sink dispatch.
Formatting is bounded; nested output is serialized deterministically; sink
exceptions retain history.

## Save, restart, and artifacts

Same-process restart requires the exact simulation, canonical exported objects,
handles, native-owner inventory, ABI, content, cache, mapped-library, and
plug-in provenance. It preserves process-local owners and returns identity
handle mappings.

Portable restore targets an already elaborated registry. It validates schema,
host and plug-in ABI versions and sizes, pointer width, content/cache identity,
mapped-library content, plug-in/host fingerprints, and every canonical object
name/kind before returning old-to-new handle mappings. Library and image paths
may relocate only when stable identities and content fingerprints still match;
relocations are reported explicitly.

Native callback closures/user data, foreign subprograms/models/user data,
active foreign calls, open files, protected leases, scheduler transactions,
output sink contexts, and dynamic-library contexts are never serialized.
Portable restore reports each as a counted invalidation that must be recreated
after verified image reload.

## Portability and limits

The C boundary uses fixed-width integers, explicit byte counts, stable
calling/export macros, and no C++ types. Independent C and C++ images must
produce identical ordered service semantics across repeated and relocated
loads. Interpreter, LLVM O0/O2, multi-root, mixed-language, cache, object/
design artifact, mapped-library, relocation, and restart owners share the same
simulation identity and scheduler rules.

VHPI is a governed compatibility surface, not a promise to serialize native
state or emulate proprietary simulator extensions. Malformed profiles,
oversize inputs, invalid/stale/foreign handles, exceptions, resource limits,
and incompatible artifacts reject without partial publication.

Primary evidence lives in `tests/runtime/runtime_vhpi_*_tests.cpp`,
`tests/runtime/vhpi_reference_plugin_c.c`, and
`tests/runtime/vhpi_reference_plugin_cpp.cpp`.
