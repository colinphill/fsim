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

`FSIM_API_VERSION` is 1. `fsim_session_t`, `fsim_object_t`, and `fsim_time_t`
are opaque 64-bit values; zero is the invalid session/object identity. All
public functions have C linkage. No C++ exception crosses the boundary; an
unexpected internal failure maps to `FSIM_STATUS_INTERNAL_ERROR`.

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
[31-symbol contract](../tests/feature_matrix/core_api_contract.tsv).

The exported symbol set is exactly:

```text
fsim_get_api_version
fsim_status_string
fsim_session_create
fsim_session_destroy
fsim_session_load_project
fsim_session_check
fsim_session_build
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

## Current persisted identities

| Family | Current identity | Top-level content |
| --- | --- | --- |
| `fsim.toml` | project schema 3 | user-authored project, source, library, build, run, trace, SDF, SystemC, and code-coverage settings |
| `.fsimobj` | `FSIMOBJ\0`, format 7, portable schema 14 | canonical metadata, v3 code-coverage identity, optional source payloads, portable owning units |
| `.fsimdesign` | `FSIMDES\0`, format 12, runtime ABI 1 | roots/bindings/provenance, v3 code-coverage identity, and checksummed runtime, semantic, DesignIR, HIR, coverage, UVM, SDF, trace, SystemC, and SCV state |
| `.fsimlib` | canonical TOML format 5, portable schema 14 | logical-library metadata, optional sources, portable units, optional exact native accelerators |
| `.fsimscobj` | `FSIMSCO\0`, format 2, runtime ABI 1, SystemC ABI 4 | one C++20 translation unit, dependency identity, and native object |
| `.fsimscplugin` | format 2, runtime ABI 1, SystemC ABI 4 | ordered object identities, link settings, sorted factory schema, and native shared library |
| LLVM object cache | `FSIM-OBJECT-CACHE-V1`, key schema `fsim-llvm-native-object-v168` | checksum-framed native object keyed by target, lowering controls, and v3 code-coverage identity; canonical lowercase SHA-256 key; 256 MiB read ceiling |

All numeric binary fields are canonical little-endian. Artifact payload paths
are relative, normalized, contained, and unique within their artifact. Trees
publish through an atomic sibling stage, become read-only, and do not overwrite
an existing destination.

Objects, designs, design-cache records, and LLVM native-object keys retain the
schema-3 code-coverage configuration and model identity. Disabled coverage uses
model `none`; enabled foundation coverage uses
`fsim-code-coverage-foundation-v3`. Versioned v2 objects and designs are
rejected directly; there is no compatibility reader or migration.

The portable owning-unit and class codecs are schema 32; UDP declarations are
schema 1. The standalone design's current state schemas are runtime 62,
semantic 4, DesignIR 4, class 12, SystemVerilog constraint HIR 7, coverage 7,
UVM 3, and VHDL HIR 4. Checkpoint envelopes are schema 1 or 2 according to the
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
       -> schema-31 owning units / schema-1 UDP declarations
  -> .fsimlib
       -> optional sources + portable units
       -> optional exact LLVM/SystemC native accelerators

ordered .fsimobj + selected .fsimscplugin + roots/bindings
  -> .fsimdesign
       -> checksummed portable runtime/semantic/HIR/coverage/UVM/SDF/trace state
       -> embedded selected SystemC plug-ins with exact producer identity

SystemC source + dependencies
  -> .fsimscobj ... -> ordered link -> .fsimscplugin
```

Portable identity includes the logical language, exact VHDL/Verilog/
SystemVerilog revision and compatibility profile, library, source/unit content
digests, package dependencies, selected roots, bindings, and nested schema
identities. Producer absolute paths are excluded; logical and artifact-relative
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
# Recreate a portable HDL object from source.
fsim compile --lang systemverilog --standard 2017 --library work \
  --output unit.fsimobj source.sv

# Recreate a standalone design from current objects and explicit roots.
fsim elaborate --object unit.fsimobj --top top=sv:work.top \
  --output design.fsimdesign

# Recreate an incremental SystemC object and linked plug-in.
fsim systemc compile --output bridge.fsimscobj bridge.cpp
fsim systemc link --object bridge.fsimscobj --library models \
  --output models.fsimscplugin

# Recreate a mapped library from a current project build.
fsim build -p fsim.toml --export-library work=work.fsimlib
```

For `fsim.toml`, recreate or update the source manifest with top-level
`schema = 3`; do not copy a schema-0/1 file forward without revalidating all
current fields. For a native cache mismatch, remove only the affected cache
root or let a distinct content key miss and repopulate it. Do not edit cache or
artifact metadata to claim compatibility.

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
platform cells for every ledger row. Each of the 198 cells names one generated
CTest, one cataloged diagnostic, and checked-in implementation and evidence
paths. Exact generated commands have one ordinary-regression owner; recursive
closure summaries consume fixture witnesses instead of starting nested CTest
regressions.
Unsupported identity wording is frozen by the
[schema/producer diagnostic contract](../tests/feature_matrix/schema_producer_diagnostic_contract.tsv),
and corruption/locking/build-identity behavior is frozen by the
[cache isolation contract](../tests/feature_matrix/cache_corruption_isolation_contract.tsv).
