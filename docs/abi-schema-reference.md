<!-- SPDX-License-Identifier: Apache-2.0 -->

# fsim v3 ABI and schema reference

This is the compact reference for fsim v3 public ABIs, installed targets,
persisted formats, native-cache identities, compatibility rules, and recovery
workflows. The checked contracts linked below are normative for exact fields,
offsets, limits, and evidence. This page explains how those boundaries compose.

## Compatibility policy

fsim v3 distinguishes public native ABIs from persisted development artifacts:

- Public C and plug-in records are versioned, size-prefixed where specified,
  and append-only. A consumer validates the required prefix before using it.
- Portable and native artifact readers are exact-current readers. They do not
  migrate, downgrade, infer omitted legacy fields, or dual-write old formats.
  The workspace library catalog accepts only SQLite schema 4.
- Portable payloads are independent of the producer host when all declared
  language, standard, compatibility, schema, and content identities match.
- Native payloads and caches require the exact producer identity. They are
  accelerators, never a compatibility substitute for portable content.
- An unsupported identity rejects transactionally before partial metadata,
  registry, native-image, cache, or destination publication. The diagnostic
  names the artifact family, found identity, required identity, and current-
  build regeneration action.

The supported object model is x86-64 Linux LP64 or Windows LLP64: pointers and
`size_t` are 8 bytes, public enums are 4 bytes, and the maximum governed public
alignment is 8 bytes. Foreign functions use the platform-default C calling
convention (`__cdecl` on Windows).

## Installed packages and targets

| Package | Exact identity | Consumer surface |
| --- | --- | --- |
| `fsim` | API version 1 | `fsim::api`, `fsim/api.h`, C11 or C++20, and `FSIM_SHARED` |
| `SystemCLanguage` | 3.0.2.20251031 | `SystemC::systemc` and the installed SystemC headers/shared runtime |
| `SystemCTLM` | 2.0.6.20191203 | TLM headers carried by the exact SystemC language package |
| `SCV` | 2.0.1 | `SCV::scv`, exact SystemC dependency, and installed SCV headers/shared library |

The SystemC/TLM and SCV packages also install relocatable `systemc.pc`,
`tlm.pc`, and `scv.pc` metadata. A relocated consumer must resolve exactly one
governed SystemC shared runtime. Build-tree paths are not part of an installed
target's interface.

## Core C API

`FSIM_API_VERSION` is 2. `fsim_session_t`, `fsim_object_t`, and `fsim_time_t`
are opaque 64-bit values; zero is the invalid session/object identity. All
public functions have C linkage. No C++ exception crosses the boundary; an
unexpected internal failure maps to `FSIM_STATUS_INTERNAL_ERROR`.

Version 2 replaced the project calls (`fsim_session_load_project`,
`fsim_session_check`, `fsim_session_build`) with
`fsim_session_open_snapshot(session, workspace_dir, snapshot)`. It loads an
elaborated workspace snapshot as `fsim simulate` does, including the
`simulate.*` defaults of the per-user and workspace `fsim.tcl`, unless the
session options set `FSIM_SESSION_FLAG_NO_CONFIG` or the environment sets
`FSIM_NO_CONFIG=1`. The exported symbols carry the `FSIM_2.0` version node.

The current x86-64 layouts are:

| Type | Size | Alignment |
| --- | ---: | ---: |
| `fsim_string_view_t` | 16 | 8 |
| `fsim_session_options_t` | 32 | 8 |
| `fsim_diagnostic_t` | 72 | 8 |
| `fsim_object_info_t` | 208 | 8 |
| `fsim_mapped_library_info_t` | 88 | 8 |
| `fsim_sdf_input_t` | 72 | 8 |
| `fsim_sdf_options_t` | 40 | 8 |
| `fsim_sdf_summary_t` | 88 | 8 |
| `fsim_sdf_report_entry_t` | 64 | 8 |
| `fsim_trace_options_t` | 72 | 8 |
| `fsim_trace_status_t` | 104 | 8 |
| `fsim_trace_report_entry_t` | 64 | 8 |
| `fsim_safe_point_info_t` | 168 | 8 |
| `fsim_callbacks_t` | 56 | 8 |

Every extensible record begins with the 8-byte `struct_size`/API-version
header. The retained v1 prefixes are 88 bytes for `fsim_object_info_t` and 48
bytes for `fsim_callbacks_t`. Exact field offsets, enum values, callback types,
and C/C++ compile probes are frozen by the
[core API ABI probe](../tests/api/api_abi_contract.h) and
[29-symbol contract](../tests/feature_matrix/core_api_contract.tsv).

The exported symbol set is exactly:

```text
fsim_get_api_version
fsim_status_string
fsim_session_create
fsim_session_destroy
fsim_session_open_snapshot
fsim_session_configure_sdf
fsim_session_get_sdf_summary
fsim_session_get_sdf_report_entry
fsim_session_configure_trace
fsim_session_get_trace_status
fsim_session_get_trace_report_entry
fsim_session_flush_trace
fsim_session_close_trace
fsim_session_root
fsim_session_find_object
fsim_session_visit_children
fsim_session_get_object_info
fsim_session_mapped_library_count
fsim_session_get_mapped_library_info
fsim_session_read_value
fsim_session_deposit
fsim_session_force
fsim_session_release
fsim_session_run
fsim_session_step
fsim_session_request_stop
fsim_session_set_callbacks
fsim_session_get_diagnostic
fsim_session_diagnostic_count
```

Input string views are caller-owned for the call. Output views are session-
owned until the next session mutation. Callback arguments are borrowed for the
callback; callback user data remains caller-owned.

## Native plug-in and foreign ABIs

| Boundary | Current identity | Required layout or symbol |
| --- | --- | --- |
| SystemC | ABI 4 | `fsim_plugin_init_v1`; value view 32/8, host 152/8, registrar 32/8 |
| DPI | ABI 1 | `fsim_dpi_plugin_descriptor_v1_get`; descriptor 32/8 |
| VPI | host v1/v2, plug-in v1 | `fsim_vpi_plugin_bind_v1`; error 32/8, host v1 40/8, request 48/8, result 40/8, host v2 56/8, plug-in 48/8 |
| VHPI | host v1/v2, plug-in v1 | `fsim_vhpi_plugin_bind_v1`; the same sizes as VPI with a distinct handle domain |
| UVM foreign | ABI 1 | snapshot 48/8, record 72/8, activity 88/8, host 64/8 |

The SystemC host's SCV identity starts at offset 144; registrar factory and
parameter callbacks start at offsets 16 and 24. The DPI descriptor stores ABI,
size, pointer width, flags, name size, and name pointer at offsets 0, 4, 8, 12,
16, and 24. VPI/VHPI handles are stable simulation-qualified 64-bit identities,
not host addresses. Complete offsets, callback signatures, ownership, and load
order are frozen by the [SystemC ABI contract](../tests/feature_matrix/systemc_abi_contract.tsv)
and [foreign ABI contract](../tests/feature_matrix/foreign_abi_contract.tsv).

The host validates its own required ABI prefix before opening a plug-in where
that ordering is available. It then resolves the one versioned entry point,
validates the returned record, and publishes registrations only after the
whole bind succeeds. Shutdown precedes unload.
SystemC roots shut down with their simulation, while SystemC image handles stay
mapped through process teardown because Accellera registries retain native
type information.

## Current persisted identities

| Family | Current identity | Top-level content |
| --- | --- | --- |
| `.fsimobj` | one SQLite container (kind `fsim-object`) holding format 10, portable schema 16, compiled-HIR bundle 3 | canonical metadata, v3 code-coverage identity, compiled semantic/SV/VHDL HIR, and source paths (relative to the source root: the compiling workspace, or the base directory outside one; absolute only for a Windows source on another drive or share) with content digests and coverage facts, never source text |
| `.fsimdesign` | one SQLite container (kind `fsim-design`) holding format 18, runtime ABI 1 | roots/bindings/provenance, v3 code-coverage identity, the configuration settings it was elaborated with (key, value, origin), one checksummed hierarchy-path table shared by runtime and DesignIR, and checksummed runtime, semantic, DesignIR, HIR, coverage, UVM, class-specialization, SDF, trace, SystemC, and SCV state. Each payload checksum is `xxh64:` and sixteen lowercase hex digits (XXH64, seed 0; the integrity check simulate repeats at load) or lowercase SHA-256 hex |
| `.fsimlib` | one SQLite container (kind `fsim-library`) holding canonical TOML format 8, portable schema 16, compiled-HIR bundle 3 | logical-library metadata, compiled semantic/SV/VHDL HIR, source paths with content digests and coverage facts (never source text), and optional exact native accelerators |
| Workspace library catalog | SQLite schema 4 | artifact and dependency indexes with VHDL identifier-form columns; artifacts in analysis order (a recompiled artifact is the most recently analyzed), which selects an entity's latest architecture across files |
| `.fsimscobj` | one SQLite container (kind `fsim-systemc-object`) holding format 2, runtime ABI 1, SystemC ABI 4 | one C++20 translation unit, dependency identity, and native object |
| `.fsimscplugin` | one SQLite container (kind `fsim-systemc-plugin`) holding format 2, runtime ABI 1, SystemC ABI 4 | ordered object identities, link settings, sorted factory schema, and native shared library |
| Object caches | one SQLite database per cache (`static-kernel.sqlite3`, `compiled-hir.sqlite3` in the cache directory), application id `FSIM`, cache schema 1: one row per entry with its key, `xxh64:<16 hex>` payload checksum, size, last use and payload; a database of another cache schema is rebuilt; LLVM key schema `fsim-llvm-native-object-v169`; ordinary payload compiled-HIR bundle 3 | native-object and compiled-HIR caches; canonical schema-bearing lowercase SHA-256 key; 256 MiB payload ceiling; every load verifies the checksum and a corrupt entry is a miss |

`UnitIndexEntry` is a public C++ metadata record. Appending the two
`VhdlIdentifierForm` members changes its object layout. The public C++
`VhdlBindingIndication`, `VhdlBlockConfiguration`, and `Instance` records also
gain VHDL spelling and form fields in `design_core.hpp`. C++ SDK consumers must
rebuild against these headers. The C API and plug-in record layouts do not
change. `.fsimlib` format 7/schema 16 writes `name_form` and
`primary_name_form` as `unknown`, `basic`, or `extended` for each unit index.
`.fsimobj` format 9/schema 16 writes the corresponding two little-endian
32-bit enum values per unit and includes them in compilation identity.
Earlier identities (`.fsimobj` format 8/schema 15, `.fsimlib` format 6/schema
15, catalog schema 1) are rejected like any other stale identity. The
compiled-HIR schema remains 3 because the HIR already carries the identifier
form. The catalog stores both forms as checked integer values; a catalog
before schema 4 has no analysis order and is rejected as made by an older fsim.

Objects and exported libraries record source paths relative to the source
root they were compiled in, so where an artifact is saved, or where its
checkout is moved, does not change its bytes. A Windows source on another
drive or share than that root has no relative path and is recorded by its
absolute path (`D:/src/top.sv`, `//server/share/top.sv`); a path with a root
but no drive or share is never accepted. Relative paths are found by
canonical spelling too, so an 8.3 short name, substituted drive, or symbolic
link in the root does not reach the artifact. On load, an object inside a
workspace library (`<root>/.fsim/libraries/<library>/`) resolves its paths
against that workspace, so a mapped library finds its own sources; any other
object or library resolves them against the consumer's base directory. When
such an object names a file that exists there with other contents than were
compiled, elaboration warns once per object (`FSIM-ART-0006`).

All numeric binary fields are canonical little-endian. Every artifact is one
SQLite file (application id `0x4653494D`, container schema 1) whose `meta`
table names its kind and whose `payloads` table holds each payload as
checksummed chunks. Payload names are relative, normalized, contained, and
unique within their artifact. An artifact is written to a temporary file beside
its destination and renamed into place; it is never made read-only. These
low-level publishers require a fresh destination. The workspace layer names
the physical files and atomically replaces the active library or snapshot
catalog entry when compilation, linking, or elaboration succeeds. CLI users
select libraries and snapshot names; they do not name these artifact files.

Objects, designs, design-cache records, and LLVM native-object keys retain the
schema-3 code-coverage configuration and model identity. Disabled coverage uses
model `none`; enabled foundation coverage uses
`fsim-code-coverage-foundation-v3`. Versioned v2 objects and designs are
rejected directly.

The compiled-HIR bundle is schema 3 and embeds semantic schema 4,
SystemVerilog HIR schema 14, and VHDL HIR schema 8. The standalone design's
other current state schemas are runtime 92, DesignIR 5, coverage 7, UVM 3, and
class specializations (`FSIMSVCS`) 1. Runtime schema 92 stores packed values as
their width and raw bit planes. The standalone `FSIMSVCH` constraint-HIR state
is schema 14. The runtime schema carries opaque design-local package owner IDs on package
string/container objects and initializer process rows. Elaboration derives the
same token as `UnitId.value() + 1` for both sides, reserving zero for unowned
objects and ordinary processes. A startup process row also lists, as a
canonical owner-token list, the other packages it may write: empty for a
package variable initializer, which writes only its own package's objects,
and the packages its class depends on for a class static initializer or for
the constructor a package class-handle initializer (`C h = new`, a startup
process like any other) runs. Runtime admission requires each written
object's owner to be the row's owner or listed, plus canonical per-process
object-ID closures. Ownership does not depend on
library, package, or escaped-member display spelling. The closure currently
covers direct string writes and
synchronous whole-container writes emitted by the initializer call graph,
including source forms lowered as temporary container read/modify/write
followed by a guarded whole-object write. Deferred container writes and direct
`WriteContainerObjectElement` operations remain outside this support boundary.
This does not establish support for every source-level element, member,
dynamic-part, or associative update.
Runtime and DesignIR references use the canonical `FSIMHPT1` hierarchy-path
table, schema 1, stored at `state/hierarchy-paths.bin`; standalone DesignIR
and runtime payloads carry their own inline schema-1 table when serialized
outside a standalone design artifact.
Checkpoint envelopes are schema 1 or 2 according to the
typed checkpoint family. SDF application records use explicit schema 1, 2, or
4 owners; trace archives use schema 1, and the clean-room FST container carries
its own fixed v2 container identity. SCV artifact/cache, protocol, transport,
transaction, and backend records are schema 1.

The exhaustive owner-to-schema and resource-limit mapping is the
[nested portable contract](../tests/feature_matrix/nested_portable_contract.tsv).
The top-level formats and composition rules are frozen by the
[portable object contract](../tests/feature_matrix/portable_object_contract.tsv),
[design/library contract](../tests/feature_matrix/design_library_contract.tsv),
and [incremental native contract](../tests/feature_matrix/incremental_native_contract.tsv).

## Composition and provenance

```text
source + selected language profile
  -> .fsimobj
       -> optional source payloads
       -> one compiled semantic/SV/VHDL HIR bundle
  -> .fsimlib
       -> optional sources + the identical compiled-HIR bundle
       -> optional exact LLVM/SystemC native accelerators

ordered .fsimobj + selected .fsimscplugin + roots/bindings
  -> .fsimdesign
       -> shared checksummed hierarchy-path table
       -> checksummed portable runtime/semantic/HIR/coverage/UVM/SDF/trace state
       -> embedded selected SystemC plug-ins with exact producer identity

SystemC source + dependencies
  -> .fsimscobj ... -> ordered link -> .fsimscplugin
```

Portable identity includes the logical language, exact VHDL/Verilog/
SystemVerilog revision and compatibility profile, library, source/unit content
digests, package dependencies, selected roots, bindings, and nested schema
identities. Producer absolute paths are excluded, except the Windows drive or
share path of a source with no path relative to its root; logical and artifact-relative
source coordinates remain available to diagnostics, debugger, API, and trace.

Native LLVM identity additionally includes build configuration, LLVM version,
runtime/frame/result ABI versions and sizes, target triple, data layout, CPU,
sorted feature set, O0/O2 mode, and source specialization key. Native SystemC
identity additionally includes build configuration, compiler requested/resolved
path and content, environment, toolchain family, target/host format, standard
library, runtime/SystemC/TLM/SCV identities, MSVC runtime/member-pointer model,
options, dependencies, and source content. Any difference selects another key
or rejects indexed native admission before payload read/image open.

Interpreter execution does not consume an LLVM native object. Portable design
state is shared, while LLVM O0 and O2 use separate native identities.

## Rejection and transactionality

Readers reject bad magic, stale or future format/schema/ABI, truncation,
trailing bytes, oversized fields, invalid enums, unsafe paths, duplicates,
checksum/digest inconsistency, missing payloads, and incompatible producer
identity. Resource ceilings are checked before allocation or publication.
Design format 13, runtime schema 63, and DesignIR schema 4 are stale and are
rejected without compatibility decoding. A missing required hierarchy-path
payload kind reports `FSIM-ART-0011`; a malformed or checksum-mismatched
payload reports `FSIM-ART-0014`. Old nested state identities report
`FSIM-ART-0013` with the found and required schema and a `.fsimdesign`
regeneration instruction.

Portable mismatch diagnostics use the project/object/design/library or nested
artifact family. Native mismatch diagnostics name the compiler, target,
runtime, ABI, LLVM/SystemC/SCV, optimization, or host fingerprint involved.
Rejected operations return no partially decoded object and do not mutate a
registry, open/admit a known-incompatible image, publish a cache entry, or leave
a destination/staging tree.

## Regeneration and rebuild workflows

There is no supported in-place migration command and no legacy compatibility
reader. Recover from an identity mismatch by rebuilding from the nearest
available source or portable boundary with the current fsim build:

```sh
# Replace this source's managed HDL units in work.
fsim compile --lang systemverilog --standard 2017 --library work source.sv

# Replace the default snapshot using library metadata to find its top.
fsim elaborate --top work.top
fsim simulate

# Replace managed SystemC objects, then refresh the factory catalog.
fsim systemc compile --library models bridge.cpp helper.cpp
fsim systemc link --library models
fsim elaborate --top models.bridge --snapshot bridge
fsim simulate --snapshot bridge

# Reuse another workspace's managed library directory.
fsim library map models /path/to/producer/.fsim/libraries/models
```

The same source selection replaces the affected managed objects; other library
objects remain available. Re-link after changing or deleting a SystemC object.
Published snapshots retain their embedded native images until re-elaboration
replaces the selected snapshot. See [workspace mode](workspace-mode.md).

The retained C++ artifact publishers and project configuration reader are
low-level SDK interfaces. Callers using them still supply fresh artifact
paths, and the configuration reader requires schema 3. They do not define the
public CLI workflow. For a native cache mismatch, let a distinct content key
miss and repopulate the cache. Do not edit cache or artifact metadata to claim
compatibility.

If a source-hidden artifact is incompatible and no source or current portable
payload exists, use the exact producer build that created it or reacquire the
source/current portable artifact. fsim cannot safely reconstruct omitted state
and intentionally provides no fallback reader.

## Audit anchors

The one-to-one Batch 174 boundary and evidence owners are in the
[ABI/schema inventory](../tests/feature_matrix/abi_schema_inventory.tsv).
Its exhaustive
[evidence matrix](../tests/feature_matrix/abi_schema_evidence_matrix.tsv)
owns positive, single-field mutation, stale/future version, corruption,
truncation, resource, relocation, read-only, source-hidden, toolchain, and
platform cells for every ledger row. Change 9, the project-manifest
stale-schema policy, was retired with the manifest and has no row. Each of
the 187 cells names one generated
CTest, one cataloged diagnostic, and checked-in implementation and evidence
paths. Exact generated commands have one ordinary-regression owner; recursive
closure summaries consume fixture witnesses instead of starting nested CTest
regressions.
Unsupported identity wording is frozen by the
[schema/producer diagnostic contract](../tests/feature_matrix/schema_producer_diagnostic_contract.tsv),
and corruption/locking/build-identity behavior is frozen by the
[cache isolation contract](../tests/feature_matrix/cache_corruption_isolation_contract.tsv).
