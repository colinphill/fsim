<!-- SPDX-License-Identifier: Apache-2.0 -->
# Public tracing API

All public tracing surfaces apply one immutable request and report one common
status. They do not expose writer implementation objects, native handles, host
compression state, or producer paths.

## Stability and ownership

The installed, cross-build native ABI is the C11/C++-callable `fsim::api`
target and `fsim/api.h`. Its trace records are size-gated append-only prefixes;
the [ABI and schema reference](abi-schema-reference.md) owns their exact sizes,
symbols, lifetimes, and rejection policy.

The C++ types below are the source-level application control model used by the
CLI, Tcl, debugger, tests, and C adapter. They are tied to the exact fsim source
and build and do not promise a stable C++ binary layout. No C++ exception,
`std::filesystem::path`, writer object, or borrowed native handle crosses the
installed C ABI. Inputs are copied or validated before publication; status and
report views remain owned by the simulation or returned value documented by
their surface.

## C++ control

`include/fsim/app/trace_api.hpp` defines `TraceControlRequest`,
`TraceControlStatus`, `TraceControlReportEntry`, `TraceControlApplication`,
and `TraceRuntime`. A request identifies its surface and compile/elaborate/
simulate phase, output intent, `auto`/`vcd`/`fst` format,
`auto`/`none`/`deterministic` compression, selection, lifecycle, generation,
and report limit.

```cpp
fsim::app::TraceControlRequest request;
request.surface = fsim::app::TraceControlSurface::CppApi;
request.phase = fsim::app::TraceControlPhase::Simulate;
request.output = "waves.fst";
request.format = fsim::project::TraceFormat::fst;
request.compression = fsim::project::TraceCompression::deterministic;
request.selection = {"top.*"};

auto result = fsim::app::apply_trace_control(std::move(request));
if (!result.ok()) {
  // result.diagnostics contains one stable transactional failure.
}
```

`trace_control_request` converts a project run section and
`publish_trace_control` publishes a validated request back into it.
`TraceRuntime::attach`, `status`, `flush`, `close`, and `fail` own live
simulation lifecycle. `TraceControlLimits` bounds selections, report entries,
output text, and aggregate selection text before publication.

## Native C control

`include/fsim/api.h` exposes append-only, size-gated structures and these
operations:

- `fsim_session_configure_trace`
- `fsim_session_get_trace_status`
- `fsim_session_get_trace_report_entry`
- `fsim_session_flush_trace`
- `fsim_session_close_trace`

`fsim_trace_options_t` carries format, compression, lifecycle, phase, output,
selection array, report limit, and generation. `fsim_trace_status_t` reports
requested/effective profiles, lifecycle, selection/report counts, truncation,
output, and semantic identity. Report entries are typed as output, format,
compression, selection, or lifecycle. Callers must initialize `struct_size`
and `api_version`; fsim never reads or writes beyond the advertised prefix.

## CLI, Tcl, and debugger

CLI controls are `--trace`, `--trace-format`, `--trace-compression`, repeatable
`--trace-filter`, `--trace-lifecycle`, and `--trace-report-limit`. They are
valid for project build/run/debug and the owning manifest-free phases.

Tcl uses `fsim::trace configure|disable|status|report|add|remove|all|clear|list`.
Debugger selection uses `trace add|remove|all|clear|list|status`. These paths
share the same selection limits, canonical identities, lifecycle transitions,
and diagnostics as C/C++.

## Archive and reader APIs

`include/fsim/app/trace_archive.hpp` encodes and decodes a versioned
`TraceArchiveSnapshot` for object, design, library, native-cache, and checkpoint
owners. `make_trace_archive_snapshot`, `encode_trace_archive`,
`decode_trace_archive`, and `restore_trace_archive_control` validate schema,
profile compatibility, checksums, bounds, and relocation before publishing a
request.

`include/fsim/runtime/fst_reader.hpp` provides bounded clean-room
`read_fst(span)`, `read_fst(string_view)`, and `read_fst_file(path)` overloads.
On success, `FstReaderTrace` contains canonical scopes, declarations, aliases,
timestamps, typed values, compression identity, and semantic digest. On
failure, `FstReaderResult` contains no trace and exactly one stable diagnostic.

See [VCD and FST tracing](tracing.md) for configuration, supported values,
determinism, lifecycle, and executable evidence.

## Release qualification boundary

The installed C ABI and exact-build C++ surfaces above are current v2
contracts; platform qualification is recorded separately. The frozen
[support matrix](../packaging/v2-support-matrix.tsv) and
[candidate record](../packaging/v2-release-record.txt) do not infer Windows or
Release behavior from local Linux Debug execution. Final Batch 177 owns every
Release build/test/gate, sanitizer and hosted Linux/Windows result.
