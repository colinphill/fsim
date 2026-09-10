<!-- SPDX-License-Identifier: Apache-2.0 -->
# SystemVerilog VPI support

fsim v3 provides a simulation-owned, versioned VPI boundary for selectable
Verilog-1995/2001/2001-noconfig/2005 and
SystemVerilog-2005/2009/2012/2017/2023 profiles. The implementation uses stable
integer identities and owning C++ services; neither a plug-in nor an artifact
receives addresses of simulator objects.

This API is part of the governed SystemVerilog-2017 closure surface. Vector
descriptors preserve every host-addressable word and state plane; their
explicit `uint32_t` width representation is a host ABI boundary, not a
SystemVerilog packed-width limit. The exact cross-engine, artifact, relocation,
checkpoint, and public-API witnesses are indexed by the
[SystemVerilog release audit](v1-systemverilog-release-audit.md).

## ABI and plug-in lifecycle

The public C header is
`include/fsim/runtime/vpi_abi.h`. The original 40-byte v1 host is frozen and
supports bounded diagnostic reporting. The 56-byte v2 host begins with that
complete v1 table and appends a service context plus one C-compatible invocation
callback. Every table and request carries an explicit size. A plug-in must
validate `abi_version` and `struct_size` before reading a field.

The direct v3 model exports `fsim_vpi_plugin_bind_v1` using `FSIM_VPI_EXPORT`
and `FSIM_VPI_CALL`. Loading validates the host, opens the image, resolves the
exact symbol, contains bind/startup exceptions, validates the returned
descriptor, and publishes one move-only owner only after successful startup.
Explicit shutdown and destruction invoke its plug-in shutdown exactly once
before the image unloads. C and C++ reference images exercise that contract.

A conventional VPI image may instead export the standardized
`vlog_startup_routines` table. Fsim recognizes that ordered null-terminated
table when the direct bind symbol is absent, invokes each entry once under a
4,096-entry ceiling, and publishes the image only after the table completes.
The standardized model has no custom shutdown callback; registered end-of-
simulation callbacks own language-level teardown while the image lifetime
still remains with the same move-only loader object. A C reference image proves
the standard entry model independently of the direct v3 descriptor.

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

The common 2023 routine bridge owns a 42-entry, index-stable catalog covering
callback and system-callable registration, hierarchy and multidimensional
lookup, property/value/delay/time access, handle lifetime and comparison,
opaque data and user data, control, diagnostics, formatted output, MCDs, and
files. Each entry maps to exactly one of the sized service families. The bridge
rejects a truncated host or request, mismatched family, forbidden or missing
handle, incoherent or excessive text, failed or throwing callback, truncated
result, reserved result bits, unknown status, or required null result handle.
Validated failures use the bounded host error view and stable
`FSIM-VPI-ROUTINE-001` through `-007` diagnostics. The normative C wrappers
are layered over this boundary rather than duplicating validation.

## Object and value model

Object handles encode simulation registry, slot, and generation. The complete
2023 taxonomy is append-only: the original root-through-min/typ/max identities
remain fixed at 0 through 21, and hierarchy arrays, primitives, callables,
statements, assertions, coverage, declarations, types, selections, calls, and
attributes follow without renumbering them. Every object retains its parent,
creation ordinal, live-child count, canonical full name, optional source, and
exact type metadata.

Generic property queries return owning strings and explicitly typed object,
handle, integer, and Boolean results. Missing optional metadata is distinct
from an unsupported property. Typed object iteration and relationship
iteration cover parents, child scopes, declarations, ports, nets, variables,
parameters, processes, assertions, drivers, expressions, arguments, types,
and coverage objects in canonical creation order. Iterators capture bounded
snapshots, but scan revalidates generation and liveness so a released object
is never returned as live. Lookup, traversal, release, stale reuse, and cross-
simulation use return distinct errors.

Scalar and recursive type descriptors cover packed/unpacked arrays, dynamic
arrays, queues, associative arrays, structs, unions, enums, strings, classes,
and class handles within documented resource limits. Checked values cover
two-, four-, and nine-state vectors, integers, real/shortreal, strings, time,
and strengths. Deposits update the underlying value; force is a separate
visible layer; deposits continue beneath force; release reveals the latest
underlying value. Delayed deposit/force/release use the common scheduler.

Enum literal identity uses the same owning arbitrary-width `PackedLogic4`
representation as live packed values. Descriptor validation requires every
literal to match the declared width exactly, preserves signedness and four-state
`X`/`Z` planes, rejects unknown bits for two-state enums, and detects duplicate
names or exact values without projecting through a host integer.

## Data-read service

Each simulation owns one limited-interactive data reader over the same VPI
object registry. The reader does not republish hierarchy, connectivity, source,
or type records: ordinary design handles continue to resolve through the
generation-qualified registry, while reader-owned traverse and collection
handles carry a separate extension identity. Post-process readers accept
independently decoded histories, and history-preserving interactive readers can
observe changes before an object is selected.

Load initialization supports an object collection or a hierarchy scope with a
bounded recursion depth. It selects only value-bearing ports, nets, variables,
parameters, memories, primitive contributions, assertions, and their indexed
forms. A zero depth selects the complete subtree. Collection loads stage only
the affected object histories and publish the entire selection atomically;
they never take a whole-registry or whole-history snapshot. Explicit load and
unload retain the same rule, and unloading prevents new traverse creation
without invalidating already-created readers.

Object and traverse collections preserve insertion order and suppress duplicate
members. Filters accept one ordinary object kind or one reader Boolean property
and create a new collection without modifying the source. Single traverses can
move to the retained minimum, maximum, previous change, next change, or a
specific time. Collection traversal advances all members to one common time;
`HasValueChange` filtering then identifies exactly the members changing there.
Time jumps retain the most recent value at or before the requested time and
report `HasNoValue` before the first sample.

Every database name, loaded-object set, change history, collection, member set,
and traverse population has an explicit ceiling. Samples are type-checked and
monotonic in time/delta order. Invalid, cross-extension, released, unloaded,
closed, wrong-kind, out-of-order, type-mismatched, and resource failures remain
distinct. Closing an extension prevents new loads and reader handles while
leaving existing traverse observations valid for orderly teardown.

Published HDL objects also carry Batch 167 provenance metadata: exact owning
semantic-unit and source identities, the canonical Verilog-1995/2001/
2001-noconfig or SystemVerilog-2005/2009/2012/2017 standard, and the independent
compatibility profile. Child signals, ports, processes and scopes inherit their
owning unit's record without exposing compiler/cache identities. The metadata
uses size-gated append-only fields and an explicit provenance flag; a partial
record is invalid rather than being published with inferred defaults.

## Time, callbacks, control, and system callables

Time queries preserve 64-bit ticks, scaled-real conversion, time unit,
precision, and delta. Value-change, delay, read-write, read-only, next-time,
synchronization, simulation, reset, save, and restart callbacks use common
scheduler regions and copied event data. Removal, self-removal, nested
registration, re-entry, user exceptions, and teardown are contained.

SystemVerilog assertion objects are children of their stable process owners.
Persistent success, failure, vacuous, disabled, and aborted callbacks carry the
assert/assume/cover/restrict kind, assertion and process identities, instance,
coverage slot, source span, action-suppression state, time, and delta. Modules,
interfaces, programs, top-level packages, classes, and class properties are
published with exact parentage and recursive type/range metadata.

The standardized assertion API observes that same completion stream. It keeps
separate saturating attempt, success, failure, vacuous, disabled, and aborted
counters for each generation-qualified assertion object; disabled observations
do not count as attempts. Global or object-selected reset, enable, disable, and
kill requests first delegate to the simulator's common assertion-control hook.
Only an accepted request changes API state. In an integrated simulation the
hook updates the same spawn filter and active-attempt collection used by HDL
`$assert*` controls, including target-specific enable overrides and kill
quiescence, so the foreign API cannot create a second assertion scheduler.
Object count and event text are bounded, callback failures are contained, and
cross-simulation, released, wrong-kind, unknown-control, resource, and common-
control failures remain distinct.

The standard coverage API shares that assertion state rather than maintaining
a second counter table. Assertion start, stop, reset, and check requests map to
the common assertion enable, disable, reset, and saturation state, and its
attempt, success, failure, vacuous, disabled, and killed properties are read
from the same completion stream. Statement coverage remains backed by the
simulation-owned code counters. Its save path writes the bounded direct-v3
`.fsimcov` container atomically; corrupt or duplicate-run merges fail without
changing history, and unavailable toggle or FSM stores publish no phantom
targets or files. All six controls, four metric types, coverage properties,
FSM relations, exact state values, iterator lifetimes, cross-simulation
rejection, integer overflow, and provider exceptions have independent API
evidence.

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

The installed SDK publishes the standard root-level `vpi_user.h` and
`sv_vpi_user.h` include names. The former owns the fixed-width C scalar types,
opaque handle, time/value/delay/callback structures, core constants, and core
routine declarations. The latter retains the SystemVerilog object, assertion,
coverage, and design-data-reader additions plus the established compatibility
spellings `vpiCoverageStop` and `vpiCoveredMax`. Including either header first,
including both, or including them through `fsim/runtime/vpi_abi.h` produces one
identical C ABI.

The `fsim_tf` library exports the declared core, array-value, assertion, and
reader entry points. Calls are legal only while the simulator has entered a
validated VPI call context. A 64-frame thread-local stack supports bounded
foreign re-entry and rejects incomplete or mismatched enter/leave operations.
Each wrapper copies only the immediate scalar fields and passes aggregate
arguments synchronously through the Change 6 checked dispatcher; it does not
copy hierarchy or value containers. Text is bounded at one MiB before host
entry. Calls outside a context return the standard null, zero, or undefined
failure value without crossing into a stale simulation.

Standard startup tables loaded with a v2 service host run inside this same
context, so registration calls reach the simulation-owned dispatcher. A v1
host remains loadable for the frozen direct ABI but does not invent a service
surface. Installed C11 and in-tree C++20 consumers freeze the x86-64 layouts,
function signatures, include order, root install names, and link ownership.

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
