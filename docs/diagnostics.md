<!-- SPDX-License-Identifier: Apache-2.0 -->
# Diagnostic code catalog

This catalog describes every literal diagnostic code emitted by the current
production sources. It documents the internal vertical slice, not the eventual
complete v1 implementation. A code identifies a diagnostic class; paths,
source locations, messages, and notes provide the instance-specific detail.
Code spellings are the stable, machine-readable part of the current
diagnostic interface; message wording may evolve.

Unsupported ABI, schema, artifact, and native-producer diagnostics follow the
transactional identity and regeneration policy in the
[v2 ABI and schema reference](abi-schema-reference.md).

The Batch 175 performance-baseline gate is a qualification/test contract, not
a production diagnostic family. Its wall-time, peak-memory and throughput
threshold failures do not introduce `FSIM-*` codes and may not suppress or
weaken the resource, overflow, cache-lock, external-process or trace errors
cataloged below. Linux Clang/GCC thresholds are checked in; current Windows
LLVM-MinGW thresholds remain pending real Batch 177 measurements. Historical
MSVC/clang-cl-only rows require an explicit migration or retirement disposition
rather than an inferred result.

Source and manifest paths carried by text or JSON diagnostics are normalized
UTF-8 generic paths on Linux and Windows. A leading UTF-8 BOM is transport
metadata rather than a token; CRLF and CR line endings retain the same logical
line accounting as LF while physical byte offsets remain source-accurate.

The severity column is the severity assigned by the current emitter. All
entries are errors unless explicitly marked as warnings. The four strings
`FSIM-OBJECT-CACHE-V1`, `FSIM-CACHE-LOCK-V1`, `FSIM-DESIGN-CACHE-V1`, and
`FSIM-DESIGN-CACHE-V2` are persistent-format markers, not diagnostics, and
are therefore excluded.

The governed VPI boundary reports ABI, object, iterator, value, callback,
control, system-callable, I/O, plug-in, and checkpoint failures through the
typed status enums in `include/fsim/runtime/vpi_*.hpp` and the bounded
`fsim_vpi_error_view_v1` C view. Those runtime statuses are not production
`FSIM-*` diagnostic codes and therefore do not add catalog rows.

The governed VHPI boundary likewise reports ABI, hierarchy, iterator, type,
value, driver/write, time/callback, foreign, association, I/O, plug-in, and
checkpoint failures through typed `VhdlVhpi*Error` enums and the bounded
`fsim_vhpi_error_view_v1` C view. Those runtime statuses are not production
`FSIM-*` diagnostic codes and add no catalog rows.

## Command line and C API

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-CLI-0001` | error | Invalid command-line argument, option, value, command, or direct-source combination. |
| `FSIM-CLI-0002` | error | The selected command has no connected implementation in this build. |
| `FSIM-CLI-0003` | error | An exception escaped command dispatch. |
| `FSIM-TCL-0001` | error | The selected build has no embedded Tcl interface. |
| `FSIM-TCL-0002` | error | The Tcl interpreter, standard library, fsim namespace, or standard channels could not be initialized. |
| `FSIM-TCL-0003` | error | A batch Tcl command or script failed during evaluation. |
| `FSIM-TCL-0004` | error | Interactive input ended with an incomplete Tcl command. |
| `FSIM-TCL-ASSERT-0001` | note/warning/error/fatal | An HDL assertion failed while a Tcl-controlled simulation was running. |
| `FSIM-TCL-REPORT-0001` | note/warning/error/fatal | A VHDL report or SystemVerilog severity task was delivered to Tcl diagnostics and callbacks. |
| `FSIM-API-0002` | error | A C API check or build was requested before loading a project. |
| `FSIM-API-0003` | error | A C API operation requires a successfully built design. |
| `FSIM-API-0004` | error | A built design contains more debug-visible local variables than the version-1 object-handle encoding can represent. |
| `FSIM-API-ASSERT-0001` | assertion severity | A false HDL assertion stopped simulation through the C API; the diagnostic carries its process, source location, severity, and message. |
| `FSIM-API-REPORT-0001` | report severity | A nonfatal VHDL report was delivered through the C API assertion callback. |
| `FSIM-API-RUN-0001` | error | Simulation invoked through the C API failed at runtime. |
| `FSIM-API-VALUE-0001` | error | A C API deposit or force value is invalid for the selected signal. |

## Project manifest

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-PROJ-0001` | error | TOML syntax or lexical error in the project manifest. |
| `FSIM-PROJ-0002` | error | A manifest value has the wrong type. |
| `FSIM-PROJ-0003` | error | Unknown manifest table or key. |
| `FSIM-PROJ-0004` | error | Duplicate table declaration or key assignment. |
| `FSIM-PROJ-0005` | error | Missing, unsupported, or out-of-range project schema; the diagnostic names the found and required identity and directs regeneration of `fsim.toml` with the current build. |
| `FSIM-PROJ-0006` | error | Required project, source-set, or binding field is missing. |
| `FSIM-PROJ-0007` | error | A manifest value is outside its accepted range or vocabulary. |
| `FSIM-PROJ-0008` | error | Project-manifest or source-glob I/O failed. |
| `FSIM-PROJ-0009` | error | A listed source or source-glob root is missing, or a glob matched nothing. |

## Common frontend and source analysis

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-FE-0001` | error | The project contains no HDL or SystemC source files. |
| `FSIM-FE-0002` | error | More than one parsed source defines the same design-unit identity. |
| `FSIM-FE-0003` | error | A parallel source-analysis task failed or produced no result. |
| `FSIM-SEM-0001` | error | Source analysis or build normalization produced an internally invalid owning semantic projection. |
| `FSIM-FE-CU-0001` | warning | A VHDL source set requested Verilog-style compilation-unit grouping; VHDL files remain independent analysis units. |
| `FSIM-FE-STANDARD-001` | error | One physical root or included dependency is consumed under incompatible Verilog/SystemVerilog standard revisions. |
| `FSIM-FE-STANDARD-002` | error | A Verilog/SystemVerilog design-unit identity is reanalyzed under a different standard revision. |
| `FSIM-FE-STANDARD-003` | error | A SystemVerilog package is imported by a unit analyzed under a different standard revision. |
| `FSIM-FE-VHORDER-001` | error | A VHDL architecture appears before its entity in manifest analysis order. |
| `FSIM-FE-VHORDER-002` | error | A VHDL package body appears before its matching package declaration. |
| `FSIM-FE-VHORDER-003` | error | A VHDL context reference names a context that has not yet been analyzed. |
| `FSIM-FE-VHORDER-004` | error | A VHDL use clause names a project package that has not yet been analyzed. |
| `FSIM-FE-VHORDER-005` | error | A VHDL configuration declaration appears before its configured entity. |
| `FSIM-FE-VHORDER-006` | error | A VHDL configuration declaration or binding names an architecture that has not yet been analyzed. |
| `FSIM-FE-VHORDER-007` | error | A VHDL entity binding names an entity that has not yet been analyzed. |
| `FSIM-FE-VHORDER-008` | error | A VHDL configuration binding names a configuration that has not yet been analyzed. |
| `FSIM-FE-VHORDER-009` | error | A VHDL library repeats a primary entity, package, configuration, or context identity. |
| `FSIM-FE-VHORDER-010` | error | A VHDL library repeats a secondary architecture or package-body identity. |
| `FSIM-FE-VHORDER-011` | error | A VHDL source reanalyzes or consumes a logical-library unit under a different VHDL standard revision. |
| `FSIM-FE-VHDECL-001` | error | A deferred VHDL package constant has no full declaration in the corresponding package body. |
| `FSIM-FE-VHDECL-002` | error | A deferred VHDL package constant and its full declaration have nonconforming subtype indications. |
| `FSIM-FE-VHDECL-003` | error | A package body redeclares a nondeferred constant from the package declaration. |
| `FSIM-FE-VHSTD-001` | error | A required compiler-supplied IEEE 1076-2019 source file is unavailable or unreadable. |
| `FSIM-FE-VHSTD-002` | error | A compiler-supplied IEEE 1076-2019 source file does not match its pinned upstream checksum. |
| `FSIM-FE-VHSTD-003` | error | A selected VHDL revision does not provide a declaration, expression, name, association, operator, context, generate, process, statement, instantiation, or port-map feature and names its minimum revision and migration, or fsim's intrinsic semantic projection of a pinned IEEE package is internally invalid for an owning revision. |
| `FSIM-FE-VHSTD-004` | error | A project source attempts to redeclare a compiler-supplied IEEE package. |
| `FSIM-FE-VHSTD-005` | error | Sources using different VHDL revisions attempt to share one compiler-supplied IEEE package environment; analyze them into separate logical libraries. |
| `FSIM-FE-VHSTD-006` | error | A requested compiler-supplied non-standard Synopsys compatibility package is unavailable or incompatible with the selected VHDL revision; select a compatible revision or remove its use clause. |
| `FSIM-ELAB-VHSTD-001` | error | A compiler-supplied IEEE intrinsic is unavailable in the owning process's selected VHDL revision and no user-defined overload owns the name. |
| `FSIM-ELAB-VHNUM-001` | error | An IEEE or Synopsys numeric function has the wrong arity or value profile. |
| `FSIM-ELAB-VHNUM-002` | error | A numeric conversion or resize result size is not locally static and representable by SimIR; standard conversions require a positive size while historical Synopsys conversions also permit zero. |
| `FSIM-ELAB-VHNUM-003` | error | `to_integer` or `conv_integer` lacks a constrained non-null signed or unsigned input profile. |
| `FSIM-ELAB-VHNUM-004` | error | A numeric conversion or resize result size differs from its contextual width. |
| `FSIM-ELAB-VHLOGIC-001` | error | A standard-logic conversion, predicate, or reduction has an unsupported argument count. |
| `FSIM-ELAB-VHLOGIC-002` | error | A standard-logic conversion or reduction has an unsupported scalar/vector width, state domain, or contextual result width. |
| `FSIM-ELAB-VHLOGIC-003` | error | A standard-logic mapping or string conversion is outside the supported static mapping profile. |
| `FSIM-ELAB-VHSYN-001` | error | Simultaneously visible `std_logic_signed` and `std_logic_unsigned` declarations provide genuinely conflicting `std_logic_vector` operator or `conv_integer` profiles; remove one use clause, qualify the call, or add an explicit numeric conversion. |
| `FSIM-ELAB-VHFIX-001` | error | A bounded fixed-point conversion or resize has the wrong arity, operand type, or value profile. |
| `FSIM-ELAB-VHFIX-002` | error | A fixed-point result range is not locally static, descending, and from 1 through 64 bits. |
| `FSIM-ELAB-VHFIX-003` | error | A fixed-point conversion, scale expansion, or rounded resize is outside the bounded static/default profile. |
| `FSIM-ELAB-VHFIX-004` | error | Fixed-point conversion or resize bounds differ from the contextual fixed-point range. |
| `FSIM-ELAB-VHFLT-001` | error | A bounded floating conversion has an unsupported argument or value profile. |
| `FSIM-ELAB-VHFLT-002` | error | A floating result does not have the reviewed binary32 `float(8 downto -23)` context. |
| `FSIM-ELAB-VHFLT-003` | error | Floating arithmetic or vector conversion is not locally static in the bounded binary32 profile. |
| `FSIM-ELAB-VHFLT-004` | error | Floating-to-integer conversion has a NaN, infinity, or out-of-range result. |
| `FSIM-FE-IO-001` | error | An HDL source file could not be opened. |
| `FSIM-FE-IO-002` | error | Reading an HDL source file failed after it was opened. |
| `FSIM-FE-IO-003` | error | HDL language inference failed for the source-file extension. |
| `FSIM-FE-LEX-001` | error | The lexer encountered an unexpected character. |
| `FSIM-FE-LEX-002` | error | A VHDL, Verilog, or SystemVerilog block comment is unterminated. |
| `FSIM-FE-LEX-003` | error | An extended identifier is unterminated. |
| `FSIM-FE-LEX-004` | error | A string literal is unterminated. |
| `FSIM-FE-LEX-005` | error | A VHDL basic identifier starts with an underscore or contains adjacent or trailing underscores. |
| `FSIM-FE-LEX-006` | error | A VHDL extended identifier is empty. |
| `FSIM-FE-LEX-007` | error | VHDL-2008 delimited-comment nesting exceeds the bounded 64-level policy. |
| `FSIM-VHDL-LEX-001` | error | A VHDL lexical form requires a later selected revision; the diagnostic identifies the required revision and exact source span. |
| `FSIM-VHDL-LEX-002` | error | A word reserved by the selected VHDL revision was used where a user identifier is required; use an extended identifier or an older compatible revision. |
| `FSIM-FE-PARSE-001` | error | A parser expectation using the common fallback code failed. |
| `FSIM-FE-PP-0001` | error | Include directories or macro definitions were supplied for a VHDL source set; these settings apply only to Verilog/SystemVerilog or SystemC. |

## Standard Delay Format frontend

These diagnostics cover Batch 168 parsing, normalization, hierarchy resolution,
schema and portable persistence. Successful resolution does not modify runtime
timing; Batches 169-170 own Verilog/SystemVerilog and VHDL/VITAL
backannotation. See [SDF support](sdf.md) for the format and API contract.

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-SDF-LEX-001` | error | An SDF source contains a non-ASCII control byte outside the permitted whitespace set. |
| `FSIM-SDF-LEX-002` | error | An SDF block comment is unterminated. |
| `FSIM-SDF-LEX-003` | error | An SDF quoted string is unterminated. |
| `FSIM-SDF-LEX-004` | error | An SDF identifier ends with an incomplete escape. |
| `FSIM-SDF-LEX-005` | error | An SDF decimal exponent has no digits. |
| `FSIM-SDF-LEX-006` | error | An SDF numeric token exceeds the configured byte limit. |
| `FSIM-SDF-LEX-007` | error | An SDF source exceeds the configured byte limit. |
| `FSIM-SDF-LEX-008` | error | An SDF token stream exceeds the configured token-count limit. |
| `FSIM-SDF-LEX-009` | error | SDF parenthesis nesting exceeds the configured depth limit. |
| `FSIM-SDF-LEX-010` | error | An SDF closing parenthesis is unmatched or the source ends inside parenthesis nesting. |
| `FSIM-SDF-LEX-011` | error | An SDF comment, string, identifier, or other token exceeds the configured byte limit. |
| `FSIM-SDF-PARSE-001` | error | An SDF root, header, or child form is missing an expected delimiter. |
| `FSIM-SDF-PARSE-002` | error | The SDF root or a DELAYFILE child has an invalid structural keyword. |
| `FSIM-SDF-PARSE-003` | error | An SDF header has invalid arity, nesting, value syntax, divider, numeric triple, or timescale. |
| `FSIM-SDF-PARSE-004` | error | An SDF header is duplicated. |
| `FSIM-SDF-PARSE-005` | error | The selected SDF revision is known but not enabled by the current parser, or is an unsupported future revision. |
| `FSIM-SDF-PARSE-006` | error | SDF headers are outside their canonical standard order. |
| `FSIM-SDF-PARSE-007` | error | A DELAYFILE has no SDFVERSION header. |
| `FSIM-SDF-PARSE-008` | error | An SDF header appears after a CELL or another body form. |
| `FSIM-SDF-PARSE-009` | error | A DELAYFILE contains an unsupported body form. |
| `FSIM-SDF-PARSE-010` | error | Tokens follow the closing DELAYFILE form. |
| `FSIM-SDF-PARSE-011` | error | A CELL form has an invalid root shape or omits its required CELLTYPE or INSTANCE child. |
| `FSIM-SDF-PARSE-012` | error | CELLTYPE does not contain exactly one quoted type name. |
| `FSIM-SDF-PARSE-013` | error | INSTANCE is not an empty selector, one exact hierarchy selector, or the sole wildcard `*`. |
| `FSIM-SDF-PARSE-014` | error | An SDF construct occurs outside the construct family that owns it. |
| `FSIM-SDF-PARSE-015` | error | A delay, timing-check, timing-environment, label, selector, or constraint construct has invalid arity. |
| `FSIM-SDF-PARSE-016` | error | A delay or constraint value is not an empty value, one decimal, or a min:typ:max triple. |
| `FSIM-SDF-PARSE-017` | error | An SDF edge form does not select exactly one port instance. |
| `FSIM-SDF-PARSE-018` | error | A delay or timing-check condition is empty, misplaced, or has an invalid nested target or label order. |
| `FSIM-SDF-PARSE-019` | error | Required CELL children or IOPATH edge, RETAIN, and delay-value children are out of order. |
| `FSIM-SDF-PARSE-020` | error | An SDF 4.0 CELL contains an unsupported nested construct. |
| `FSIM-SDF-21-001` | error | An SDF 2.1 file uses a later-revision construct, spelling, symbolic condition label, or nesting profile. |
| `FSIM-SDF-21-002` | error | Repeated SDF 2.1 INSTANCE forms or the optional CORRELATION form have invalid selector, order, multiplicity, or arity. |
| `FSIM-SDF-21-003` | error | An SDF 2.1 file mixes single delay values and min:typ:max triples. |
| `FSIM-SDF-21-004` | error | An SDF 2.1 delay list does not contain exactly 1, 2, 3, 6, or 12 values. |
| `FSIM-SDF-21-005` | error | An SDF 2.1 path, sum, diff, or skew constraint has invalid path, port, or value arity. |
| `FSIM-SDF-21-006` | error | An SDF 2.1 TIMESCALE does not use a legal 1/10/100 decimal scale with us, ns, or ps. |
| `FSIM-SDF-30-001` | error | An SDF 3.0 file uses a removed SDF 2.1 construct or spelling, or an IEEE SDF 4.0-only construct. |
| `FSIM-SDF-30-002` | error | An SDF 3.0 CELL repeats its INSTANCE form instead of using one divider-qualified selector. |
| `FSIM-SDF-30-003` | error | An SDF 3.0 TIMESCALE does not use a legal 1/10/100 decimal scale with us, ns, or ps. |
| `FSIM-SDF-30-004` | error | An SDF 3.0 delay list has invalid length or the file mixes single values and min:typ:max triples. |
| `FSIM-SDF-NORM-001` | error | An SDF decimal exponent exceeds the exact normalization range. |
| `FSIM-SDF-NORM-002` | error | An SDF value cannot be normalized without changing its empty, scalar, or min:typ:max shape. |
| `FSIM-SDF-NORM-003` | error | An SDF timescale or value has an invalid exact scale or overflows exact femtosecond scaling. |
| `FSIM-SDF-NORM-004` | error | An exact SDF hierarchy name contains an empty segment or incomplete escape. |
| `FSIM-SDF-IR-001` | error | Immutable SDF IR lowering encounters incomplete normalized timescale, instance, node, or exact-value state. |
| `FSIM-SDF-IR-002` | error | Immutable SDF IR lowering exceeds a configured cell, node, identity-byte, depth, or identity-space limit. |
| `FSIM-SDF-SCOPE-001` | error | SDF annotation scope input has incomplete normalized IR, root aliases, semantic root identities, hierarchy-divider state, or project/design identity. |
| `FSIM-SDF-SCOPE-002` | error | An SDF annotation root selection is malformed, duplicated, or absent from the elaborated design. |
| `FSIM-SDF-SCOPE-003` | error | An SDF annotation scope was created for a different project identity. |
| `FSIM-SDF-SCOPE-004` | error | An SDF annotation scope targets a stale elaborated-design identity. |
| `FSIM-SDF-SCOPE-005` | error | SDF annotation scope construction exceeds the configured root-count or semantic-identity byte limit. |
| `FSIM-SDF-RESOLVE-001` | error | SDF cell resolution encounters an incomplete/stale scope, a missing semantic parent/root, or duplicate elaborated instance ownership. |
| `FSIM-SDF-RESOLVE-002` | error | An SDF CELLTYPE and INSTANCE selector has no match in the selected elaborated roots; the message includes deterministic candidate paths and types. |
| `FSIM-SDF-RESOLVE-003` | error | A non-wildcard SDF cell selector resolves ambiguously to multiple elaborated instances. |
| `FSIM-SDF-RESOLVE-005` | error | SDF cell resolution exceeds a configured candidate, match, diagnostic-candidate, or semantic-identity limit. |
| `FSIM-SDF-ENDPOINT-001` | error | SDF endpoint resolution encounters incomplete, stale, duplicate, or structurally invalid resolved-cell or normalized-IR state. |
| `FSIM-SDF-ENDPOINT-002` | error | An SDF port, net, interconnect, device, path, timing-check, or condition endpoint has no elaborated match; the message includes deterministic candidates. |
| `FSIM-SDF-ENDPOINT-003` | error | An SDF endpoint resolves ambiguously to multiple elaborated signal or native-SystemC objects. |
| `FSIM-SDF-ENDPOINT-004` | error | An endpoint-bearing SDF construct contains no decodable endpoint and has no legal implicit device output set. |
| `FSIM-SDF-ENDPOINT-005` | error | SDF endpoint resolution exceeds a configured IR-node, candidate, mapping, endpoint, reported-candidate, or semantic-identity limit. |
| `FSIM-SDF-MAP-001` | error | Whole-SDF mapping validation encounters incomplete, stale, duplicate, structurally invalid, or nondeterministically ordered cell, IR, target, signal, or endpoint-resolution state. |
| `FSIM-SDF-MAP-002` | error | A resolved SDF mapping repeats a node/target pair, endpoint, or complete semantic annotation. |
| `FSIM-SDF-MAP-003` | error | Resolved SDF mappings conflict in object kind, direction, width, selector, conversion, semantic application key, or overlapping wildcard/exact cell ownership. |
| `FSIM-SDF-MAP-004` | error | A resolved SDF mapping is unsupported/unowned or leaves an endpoint-bearing annotation construct unconsumed. |
| `FSIM-SDF-MAP-005` | error | Whole-SDF mapping validation exceeds a configured mapping, endpoint, target, or immutable-summary identity limit. |
| `FSIM-SDF-SCHEMA-001` | error | SDF schema encoding encounters incomplete or stale syntax, normalized IR, resolved mapping, option, or compiler-compatibility ownership. |
| `FSIM-SDF-SCHEMA-002` | error | An SDF syntax, IR, endpoint, mapping, or envelope schema version is newer than this consumer supports. |
| `FSIM-SDF-SCHEMA-003` | error | An SDF schema has a stale compiler-compatibility or resolved-mapping semantic identity. |
| `FSIM-SDF-SCHEMA-004` | error | An SDF schema record is omitted, duplicated, reordered, truncated, corrupt, checksum-invalid, or contains an invalid field. |
| `FSIM-SDF-SCHEMA-005` | error | SDF schema encoding or decoding exceeds a configured envelope-byte, header, string-count, or string-byte limit. |
| `FSIM-SDF-ARTIFACT-001` | error | SDF portable artifact identity is incomplete, stale, noncanonical, incompatible with its schema/mapping, or attached to a different design digest. |
| `FSIM-SDF-ARTIFACT-002` | error | Portable design metadata contains a duplicate or conflicting SDF annotation scope/cache identity. |
| `FSIM-SDF-ARTIFACT-003` | error | SDF artifact identity exceeds its configured selected-root, semantic-unit, semantic-object, or identity-byte budget. |
| `FSIM-SDF-PORTABLE-001` | error | Portable SDF archive production encounters incomplete, stale, noncanonical, or incompatible schema, mapping, or artifact identity state. |
| `FSIM-SDF-PORTABLE-002` | error | A portable SDF archive has a future format, invalid field, corrupt checksum, truncation, trailing bytes, or mapping-to-semantic-identity mismatch. |
| `FSIM-SDF-PORTABLE-003` | error | A mapped library or portable design is missing a compatible SDF annotation payload or contains a payload incompatible with the selected design identity. |
| `FSIM-SDF-PORTABLE-004` | error | Portable SDF archive encoding, decoding, or mapped-library loading exceeds a configured byte, record, string, or identity limit. |
| `FSIM-SDF-VALUE-001` | error | SDF delay selection receives an empty/invalid exact value, invalid min/typ/max selector, zero/incompatible design time unit or simulation precision, or an invalid conversion-resource policy. |
| `FSIM-SDF-VALUE-002` | error | The selected min, typ, or max component is absent from a partial SDF delay triple. |
| `FSIM-SDF-VALUE-003` | error | Exact SDF scaling produces a negative effective delay; negative zero remains valid and retains its identity. |
| `FSIM-SDF-VALUE-004` | error | Exact SDF delay/timescale conversion exceeds its governed decimal-digit or power-of-ten expansion limit or encounters noncanonical exact input. |
| `FSIM-SDF-VALUE-005` | error | The exactly rounded SDF delay exceeds the simulator tick range. |
| `FSIM-SDF-PLAN-001` | error | SDF target planning receives incomplete/stale IR, cell, mapping, summary, cell-type, language, count, or policy state. |
| `FSIM-SDF-PLAN-002` | error | A resolved SDF annotation has no compatible elaborated timing target or has an empty/unsupported source or proposed value arity. |
| `FSIM-SDF-PLAN-003` | error | Multiple resolved SDF annotations claim the same timing-target kind, identity, and construct ownership key. |
| `FSIM-SDF-PLAN-004` | error | SDF target planning exceeds its configured annotation, values-per-annotation, or identity-byte limit. |
| `FSIM-SDF-VITAL-PLAN-001` | error | VHDL/VITAL target planning receives incomplete or stale IR, cell, mapping, summary, cell-type, language, count, or limit state. |
| `FSIM-SDF-VITAL-PLAN-002` | error | A resolved VHDL SDF cell has no unique elaborated entity, architecture, or configuration specialization binding. |
| `FSIM-SDF-VITAL-PLAN-003` | error | A resolved VHDL SDF endpoint disagrees with its elaborated port/net identity, type, width, direction, select, or language boundary. |
| `FSIM-SDF-VITAL-PLAN-004` | error | A VHDL/VITAL specialization has missing, duplicate, or inconsistent generic values and semantic identities. |
| `FSIM-SDF-VITAL-PLAN-005` | error | Multiple resolved VHDL SDF mappings claim the same instance, node, and construct ownership key. |
| `FSIM-SDF-VITAL-PLAN-006` | error | VHDL/VITAL target planning exceeds its configured target, port, generic, or identity-byte limit. |
| `FSIM-SDF-VITAL-PATH-001` | error | VHDL/VITAL path planning receives an incomplete, stale, unsupported, or non-path/check target plan. |
| `FSIM-SDF-VITAL-PATH-002` | error | A VHDL/VITAL target has no unique elaborated delay or timing-check call site within its specialization. |
| `FSIM-SDF-VITAL-PATH-003` | error | A VHDL/VITAL call kind, endpoint role, edge, or condition is incompatible with the resolved SDF construct. |
| `FSIM-SDF-VITAL-PATH-004` | error | A VHDL/VITAL path or timing-check annotation has empty, excessive, or incompatible selected-value arity. |
| `FSIM-SDF-VITAL-PATH-005` | error | A VHDL/VITAL source delay/check value is not retained as a bounded static simulator tick. |
| `FSIM-SDF-VITAL-PATH-006` | error | VHDL/VITAL path planning exceeds a record/identity limit or duplicates one stable call-site owner. |
| `FSIM-SDF-VITAL-MODEL-001` | error | VHDL/VITAL model planning receives an incomplete or stale path, target, specialization, or limit identity. |
| `FSIM-SDF-VITAL-MODEL-002` | error | A VHDL/VITAL model has missing, invalid, excessive, or inconsistent specialization process ownership. |
| `FSIM-SDF-VITAL-MODEL-003` | error | A VHDL/VITAL model exposes an unsupported or ambiguous primitive, state-table, or memory-path structural shape. |
| `FSIM-SDF-VITAL-MODEL-004` | error | A governed VHDL/VITAL wrapper registration is incomplete, unused, or disagrees with exact target port structure. |
| `FSIM-SDF-VITAL-MODEL-005` | error | VHDL/VITAL model targets, wrapper registrations, or stable call-site records duplicate ownership. |
| `FSIM-SDF-VITAL-MODEL-006` | error | VHDL/VITAL model planning exceeds its record, process, wrapper-port, or identity-byte limit. |
| `FSIM-SDF-VITAL-PRECEDENCE-001` | error | VHDL/VITAL precedence receives an incomplete source plan, conflicting min/typ/max selection, or zero limits. |
| `FSIM-SDF-VITAL-PRECEDENCE-002` | error | A repeated VHDL/VITAL annotation is stale or disagrees with its stable source call, before-value, arity, or selection identity. |
| `FSIM-SDF-VITAL-PRECEDENCE-003` | error | A VITAL timing-generic value is incomplete, ambiguous, stale, or duplicates one call/value owner. |
| `FSIM-SDF-VITAL-PRECEDENCE-004` | error | A VHDL/VITAL annotation has an unsupported absolute/increment mode or overflows its effective value. |
| `FSIM-SDF-VITAL-PRECEDENCE-005` | error | A per-revision VHDL/VITAL annotation-disable control is stale, incomplete, or duplicated. |
| `FSIM-SDF-VITAL-PRECEDENCE-006` | error | VHDL/VITAL precedence exceeds its revision, value, step, ownership, or identity-byte limit. |
| `FSIM-SDF-VITAL-SCHEDULING-001` | error | SDF VITAL scheduling lacks complete precedence input or has zero resource limits. |
| `FSIM-SDF-VITAL-SCHEDULING-002` | error | A VITAL delay call, process, instruction, source definition, shape, or ownership identity is stale or ambiguous. |
| `FSIM-SDF-VITAL-SCHEDULING-003` | error | Effective VITAL delay values are missing, noncontiguous, negative, stale, or incompatible with the retained delay shape. |
| `FSIM-SDF-VITAL-SCHEDULING-004` | error | A VITAL delay scheduling or transition policy is unsupported. |
| `FSIM-SDF-VITAL-SCHEDULING-005` | error | Transactional VITAL delay publication cannot retain a complete valid runtime design. |
| `FSIM-SDF-VITAL-SCHEDULING-006` | error | VITAL scheduled calls, values, or semantic identities exceed their configured resource limits. |
| `FSIM-SDF-VITAL-CHECK-001` | error | SDF VITAL timing-check application lacks complete scheduling input or has zero resource limits. |
| `FSIM-SDF-VITAL-CHECK-002` | error | A VITAL timing-check call, process, instruction, kind, arity, source limit, or ownership identity is stale or ambiguous. |
| `FSIM-SDF-VITAL-CHECK-003` | error | Effective VITAL timing-check values are missing, noncontiguous, negative, or inconsistent with their source call. |
| `FSIM-SDF-VITAL-CHECK-004` | error | A VITAL timing-check kind or policy is unsupported. |
| `FSIM-SDF-VITAL-CHECK-005` | error | Transactional VITAL timing-check publication cannot retain a complete valid runtime design. |
| `FSIM-SDF-VITAL-CHECK-006` | error | VITAL timing-check calls, values, or semantic identities exceed their configured resource limits. |
| `FSIM-SDF-VITAL-REANNOTATION-001` | error | VITAL live reannotation input, scope, safe-point state, pending-transaction policy, or runtime commit is invalid. |
| `FSIM-SDF-VITAL-REANNOTATION-002` | error | Equal-precedence VITAL reannotation layers select the same target and timing values. |
| `FSIM-SDF-VITAL-REANNOTATION-003` | error | VITAL reannotation changes call topology, conflicts at equal precedence, or cannot publish atomically. |
| `FSIM-SDF-VITAL-REANNOTATION-004` | error | VITAL reannotation files, targets, scope bytes, or semantic identities exceed configured resource limits. |
| `FSIM-SDF-PATH-001` | error | SDF path application receives an incomplete/stale plan, missing/repeated specify target, changed source delays, invalid application mode, or zero resource limits. |
| `FSIM-SDF-PATH-002` | error | An SDF IOPATH condition or edge qualifier is incompatible with its elaborated conditional/ifnone or edge-sensitive specify path. |
| `FSIM-SDF-PATH-003` | error | An absolute IOPATH has an unsupported delay-list arity or an incremental IOPATH does not match the current source-delay profile. |
| `FSIM-SDF-PATH-004` | error | SDF path application overflows simulator ticks or exceeds its configured path, value, or identity-byte limit. |
| `FSIM-SDF-INTERCONNECT-001` | error | SDF net/port/device timing application receives an incomplete/stale plan, repeated target identity, or zero resource limit. |
| `FSIM-SDF-INTERCONNECT-002` | error | An INTERCONNECT, PORT, MIPD, NETDELAY, or DEVICE endpoint is missing, ambiguous, stale, directionally incompatible, or outside supported Verilog/SystemVerilog objects. |
| `FSIM-SDF-INTERCONNECT-003` | error | A net/port/device annotation has no absolute/increment application mode or uses an unsupported transition-delay arity. |
| `FSIM-SDF-INTERCONNECT-004` | error | SDF net/port/device timing application exceeds its configured target, endpoint, driver-owner, value, or identity-byte limit. |
| `FSIM-SDF-MIXED-VERILOG-001` | error | Mixed VHDL/Verilog timing receives an incomplete interconnect application or zero resource limit. |
| `FSIM-SDF-MIXED-VERILOG-002` | error | Mixed VHDL/Verilog timing has missing, stale, repeated, or ambiguous conversion ownership. |
| `FSIM-SDF-MIXED-VERILOG-003` | error | A mixed boundary has incompatible direction, language, endpoint roles, resolved-value profile, or adapter topology. |
| `FSIM-SDF-MIXED-VERILOG-004` | error | Mixed VHDL/Verilog timing exceeds its boundary, driver/load-owner, or identity-byte resource limit. |
| `FSIM-SDF-MIXED-SYSTEMVERILOG-001` | error | Mixed SystemVerilog timing receives an incomplete mixed application, empty binding set, or zero resource limit. |
| `FSIM-SDF-MIXED-SYSTEMVERILOG-002` | error | A mixed SystemVerilog endpoint has a stale boundary, missing identity, or duplicate owner. |
| `FSIM-SDF-MIXED-SYSTEMVERILOG-003` | error | A mixed interface, program, class, assertion, or package endpoint lacks matching HIR ownership, widens its type, or changes event-region semantics. |
| `FSIM-SDF-MIXED-SYSTEMVERILOG-004` | error | Mixed SystemVerilog timing exceeds its binding or identity-byte resource limit. |
| `FSIM-SDF-MIXED-SYSTEMC-001` | error | Mixed SystemC timing receives an incomplete timing application, empty binding set, or zero resource limit. |
| `FSIM-SDF-MIXED-SYSTEMC-002` | error | A mixed SystemC proxy has a stale/ambiguous target, object path, or duplicate owner. |
| `FSIM-SDF-MIXED-SYSTEMC-003` | error | A mixed SystemC proxy has no supported typed signal adapter or changes its value width or time/delta sequence. |
| `FSIM-SDF-MIXED-SYSTEMC-004` | error | Mixed SystemC timing exceeds its proxy, sample, value-bit, or identity-byte resource limit. |
| `FSIM-SDF-MIXED-RESOLUTION-001` | error | Mixed resolution receives no source applications, too many sources, or zero resource limits. |
| `FSIM-SDF-MIXED-RESOLUTION-002` | error | Multiple mixed sources claim the same root/library/direction-aware path and owner identity. |
| `FSIM-SDF-MIXED-RESOLUTION-003` | error | A mixed source has an incomplete path, missing nested base, or invalid same-language direction. |
| `FSIM-SDF-MIXED-RESOLUTION-004` | error | Mixed resolution exceeds its flattened-boundary or identity-byte resource limit. |
| `FSIM-SDF-FOREIGN-001` | error | SDF foreign timing receives an incomplete VITAL/mixed application or zero resource limit. |
| `FSIM-SDF-FOREIGN-002` | error | VHPI/VPI publication would create a duplicate stable timing-object identity. |
| `FSIM-SDF-FOREIGN-003` | error | A foreign timing object has incomplete/stale identity or no effective timing values. |
| `FSIM-SDF-FOREIGN-004` | error | SDF foreign timing exceeds its object, value, or identity-byte resource limit. |
| `FSIM-SDF-VITAL-OBSERVE-001` | error | VITAL observability receives an incomplete foreign timing application or zero resource limit. |
| `FSIM-SDF-VITAL-OBSERVE-002` | error | VITAL observability receives duplicate stable foreign timing-object identity. |
| `FSIM-SDF-VITAL-OBSERVE-003` | error | A VITAL observed object or event has stale identity, invalid shape, or invalid time/delta/region order. |
| `FSIM-SDF-VITAL-OBSERVE-004` | error | VITAL observability exceeds its object, event, value, or identity-byte resource limit. |
| `FSIM-SDF-VITAL-ARCHIVE-001` | error | VITAL archive creation receives incomplete effective applications or zero resource limits. |
| `FSIM-SDF-VITAL-ARCHIVE-002` | error | A VITAL archive has stale kind, schema, generation, semantic identity, or checksum. |
| `FSIM-SDF-VITAL-ARCHIVE-003` | error | A VITAL archive has corrupt/incompatible framing, producer, policy, values, provenance, or source/effective shape. |
| `FSIM-SDF-VITAL-ARCHIVE-004` | error | VITAL archive creation or decoding exceeds record, value, string, archive, or identity resource limits. |
| `FSIM-SDF-VITAL-PHASE-001` | error | SDF VITAL phase execution receives an incomplete archive/control or zero resource limits. |
| `FSIM-SDF-VITAL-PHASE-002` | error | SDF VITAL phase execution receives an unsupported surface, phase, engine, or artifact kind. |
| `FSIM-SDF-VITAL-PHASE-003` | error | SDF VITAL phase execution cannot encode a compatible effective archive. |
| `FSIM-SDF-VITAL-PHASE-004` | error | SDF VITAL phase archive or summary exceeds its configured resource limits. |
| `FSIM-SDF-DELAY-MODE-001` | error | Ordered SDF delay-mode application receives no plan, an incomplete plan, too many plans, or a zero resource limit. |
| `FSIM-SDF-DELAY-MODE-002` | error | An SDF path/net/port/device delay list does not have a governed 1, 2, 3, 6, or 12-value transition shape. |
| `FSIM-SDF-DELAY-MODE-003` | error | Incremental SDF delay application has no current profile, uses an invalid mode, or carries stale before-values for a repeated target. |
| `FSIM-SDF-DELAY-MODE-004` | error | Ordered SDF delay application overflows simulator ticks or exceeds its configured plan, step, or identity-byte limit. |
| `FSIM-SDF-PRIMARY-CHECK-001` | error | Primary SDF timing-check application receives an incomplete plan, zero limits, or a missing, repeated, or kind-mismatched elaborated check. |
| `FSIM-SDF-PRIMARY-CHECK-002` | error | A primary SDF timing check does not retain the exact elaborated reference and data event roles. |
| `FSIM-SDF-PRIMARY-CHECK-003` | error | A primary timing-check annotation has stale or illegal signed limits, including a nonpositive setuphold/recrem combined window. |
| `FSIM-SDF-PRIMARY-CHECK-004` | error | Primary SDF timing-check application exceeds its configured check, limit, or identity-byte boundary. |
| `FSIM-SDF-SECONDARY-CHECK-001` | error | Secondary SDF timing-check application receives an incomplete plan, zero limits, or a missing, repeated, or kind-mismatched elaborated check. |
| `FSIM-SDF-SECONDARY-CHECK-002` | error | A skew, timeskew, fullskew, width, period, or nochange annotation does not retain its exact elaborated event roles. |
| `FSIM-SDF-SECONDARY-CHECK-003` | error | A secondary timing check has stale or incompatible signed limits, or a nochange start exceeds its end. |
| `FSIM-SDF-SECONDARY-CHECK-004` | error | Secondary SDF timing-check application exceeds its configured check, limit, or identity-byte boundary. |
| `FSIM-SDF-CONDITION-CHECK-001` | error | Conditional SDF timing application receives an incomplete plan or a missing, repeated, stale, or kind-mismatched timing check. |
| `FSIM-SDF-CONDITION-CHECK-002` | error | SDF timing-check reference/data roles or edge qualifiers do not match the elaborated event controls. |
| `FSIM-SDF-CONDITION-CHECK-003` | error | An SDF scalar or compound condition is absent, partial, or does not bind the exact elaborated condition-signal set. |
| `FSIM-SDF-CONDITION-CHECK-004` | error | A conditional timing check has an illegal signed window, stale/nonscalar binding, or exceeds configured check, expression, signal, or identity limits. |
| `FSIM-SDF-PULSE-001` | error | SDF pulse/RETAIN application receives an incomplete plan, invalid elaborated path profile, stale normalized node or target, or revision-incompatible global-pulse spelling. |
| `FSIM-SDF-PULSE-002` | error | Targeted and global SDF pulse annotations conflict on the same elaborated specify path. |
| `FSIM-SDF-PULSE-003` | error | A PATHPULSE, PATHPULSEPERCENT, or RETAIN threshold list has unsupported arity, duplicate ownership, or a reject value greater than its error value. |
| `FSIM-SDF-PULSE-004` | error | Exact pulse-percentage conversion or pulse application exceeds its configured tick, path, transition-value, or identity-byte limit. |
| `FSIM-SDF-PRECEDENCE-001` | error | SDF timing precedence receives an invalid command-selected delay policy or a zero effective-value or identity resource limit. |
| `FSIM-SDF-PRECEDENCE-002` | error | The command-selected min/typ/max mode conflicts with the effective SDF value selection, or enabled pulse rejection targets disabled specify paths. |
| `FSIM-SDF-PRECEDENCE-003` | error | SDF precedence finds an unsupported, missing, stale or mismatched specify-path, timing-check, delay-list or pulse/RETAIN source profile. |
| `FSIM-SDF-PRECEDENCE-004` | error | Effective SDF precedence values or their immutable semantic identities exceed the configured resource limit. |
| `FSIM-SDF-SCHEDULING-001` | error | SDF runtime scheduling receives missing or incomplete effective precedence, or a zero target, value, or identity resource limit. |
| `FSIM-SDF-SCHEDULING-002` | error | Effective runtime timing values have missing, duplicate, inconsistent, or untyped indexes and cannot form one atomic scheduled target. |
| `FSIM-SDF-SCHEDULING-003` | error | Runtime publication finds a stale, missing, incomplete, or invalid specify-path or timing-check source profile. |
| `FSIM-SDF-SCHEDULING-004` | error | Runtime SDF scheduling exceeds its configured target, value, or semantic-identity resource limit. |
| `FSIM-SDF-DRIVE-001` | error | SDF drive-state publication receives missing or incomplete scheduling, or a zero binding or identity resource limit. |
| `FSIM-SDF-DRIVE-002` | error | An annotated path or propagated continuous driver references a missing process, signal, destination, or owned region. |
| `FSIM-SDF-DRIVE-003` | error | SDF drive-state publication finds invalid switch endpoints or duplicate driver ownership. |
| `FSIM-SDF-DRIVE-004` | error | SDF drive-state bindings or their immutable semantic identities exceed the configured resource limit. |
| `FSIM-SDF-REANNOTATION-001` | error | SDF reannotation receives an incomplete baseline, file/cell scope or generation, matches no target, or attempts a runtime commit outside the annotation safe point. |
| `FSIM-SDF-REANNOTATION-002` | error | Two SDF file/cell scopes annotate the same target with identical values at equal precedence. |
| `FSIM-SDF-REANNOTATION-003` | error | SDF reannotation finds conflicting equal-precedence values, incompatible timing topology or an invalid effective timing design. |
| `FSIM-SDF-REANNOTATION-004` | error | SDF reannotation exceeds its configured file, target, scope-byte or semantic-identity resource limit. |
| `FSIM-SDF-CONTROL-001` | error | SDF control receives an invalid surface, phase, delay selection, empty input identity, missing elaborate/simulate input, or zero policy/resource limit. |
| `FSIM-SDF-CONTROL-002` | error | SDF control repeats or conflicts on one source/root/cell input scope. |
| `FSIM-SDF-CONTROL-003` | error | Annotation is requested during compile, or effective annotation phase, generation, or source/object provenance does not match the control request. |
| `FSIM-SDF-CONTROL-004` | error | SDF control inputs, source bytes, bounded report entries, or immutable semantic identity exceed the configured resource limit. |
| `FSIM-SDF-EFFECTIVE-001` | error | Effective SDF persistence receives incomplete identities, generation, policy, exact-value records, or producer-relative provenance. |
| `FSIM-SDF-EFFECTIVE-002` | error | An effective SDF object, design, library, native-cache, or checkpoint archive has a stale schema, wrong artifact kind, malformed/truncated envelope, trailing data, or checksum corruption. |
| `FSIM-SDF-EFFECTIVE-003` | error | Effective SDF persistence finds duplicate target ownership or a producer, policy, generation, target, or provenance identity incompatible with the consumer. |
| `FSIM-SDF-EFFECTIVE-004` | error | Effective SDF persistence exceeds configured record, exact-value, owned-string, archive, or semantic-identity limits. |
| `FSIM-SDF-OBSERVE-001` | error | SDF observability receives no effective targets or a target with incomplete kind, identity, source span, or before/after values. |
| `FSIM-SDF-OBSERVE-002` | error | SDF observability repeats an annotated target identity and cannot publish stable debugger/VPI/VCD object IDs. |
| `FSIM-SDF-OBSERVE-003` | error | A dynamic SDF observation references an unknown target, invalid callback region, nonmonotonic trace state, or mismatched before/after values. |
| `FSIM-SDF-OBSERVE-004` | error | SDF observation objects, preallocated surface events, values, or stable identities exceed configured limits. |

## Precompiled library artifacts

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-LIB-0001` | error | `fsim-library.toml` contains malformed or duplicate syntax. |
| `FSIM-LIB-0002` | error | The directory uses an unsupported `.fsimlib` or portable-unit schema; the diagnostic names the found and required identity and directs current-build regeneration. |
| `FSIM-LIB-0003` | error | Metadata contains an unsafe, incomplete, mismatched, duplicate, reordered, or otherwise invalid source/unit provenance value. |
| `FSIM-LIB-0004` | error | Mapped-library metadata cannot be opened or read. |
| `FSIM-LIB-0005` | error | Transactional publication, payload validation, permissions, or atomic installation failed; non-current publication identity diagnostics name found/required formats and leave no output tree. |
| `FSIM-LIB-0006` | error | A portable owning unit is malformed, incompatible, truncated, cyclic, excessively nested, contains an invalid scalar enumeration, or retains unmapped producer-absolute source provenance; schema mismatches name found/required identities and direct `.fsimobj` regeneration. |
| `FSIM-LIB-0007` | error | Project-library export cannot revalidate a source, serialize a unit, reproduce a portable SystemC build, compile/read a native variant, or complete publication. |
| `FSIM-LIB-0008` | error | A lazily selected mapped library, dependency, payload, exact unit language/standard/compatibility identity, checksum, native admission, or consumer-cache installation failed; indexed native producer mismatches name found/required host identities and direct `.fsimlib` native-payload regeneration before payload read or cache publication. |

## VHDL frontend

### VHDL syntax

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-VHDL-PARSE-001` | error | Expected a VHDL identifier, such as a unit, object, type, or selected name. |
| `FSIM-VHDL-PARSE-002` | error | Expected `is` after an entity name. |
| `FSIM-VHDL-PARSE-003` | error | Expected `(` after `port`. |
| `FSIM-VHDL-PARSE-004` | error | Expected `:` after a port name. |
| `FSIM-VHDL-PARSE-005` | error | Expected a VHDL port mode. |
| `FSIM-VHDL-PARSE-006` | error | Expected `;` between port declarations. |
| `FSIM-VHDL-PARSE-007` | error | Expected `)` after port declarations. |
| `FSIM-VHDL-PARSE-008` | error | Expected `;` after a port clause. |
| `FSIM-VHDL-PARSE-009` | error | Expected `to` or `downto` in the supported locally static range form. |
| `FSIM-VHDL-PARSE-010` | error | Expected `)` after a VHDL range. |
| `FSIM-VHDL-PARSE-012` | error | Expected `end` in a design-unit end clause. |
| `FSIM-VHDL-PARSE-013` | error | Expected `;` after a design-unit end clause. |
| `FSIM-VHDL-PARSE-014` | error | Expected `of` in an architecture header. |
| `FSIM-VHDL-PARSE-015` | error | Expected `is` in an architecture header. |
| `FSIM-VHDL-PARSE-016` | error | Expected `begin` before architecture statements. |
| `FSIM-VHDL-PARSE-017` | error | Expected `:` after a signal name. |
| `FSIM-VHDL-PARSE-018` | error | Expected `;` after a signal declaration. |
| `FSIM-VHDL-PARSE-019` | error | Expected `process` at a process statement. |
| `FSIM-VHDL-PARSE-020` | error | Expected `)` after a process sensitivity list. |
| `FSIM-VHDL-PARSE-021` | error | Expected `begin` before process statements. |
| `FSIM-VHDL-PARSE-022` | error | Expected `end` after process statements. |
| `FSIM-VHDL-PARSE-023` | error | Expected `;` after a process statement. |
| `FSIM-VHDL-PARSE-024` | error | Expected `end` after an `if` statement. |
| `FSIM-VHDL-PARSE-025` | error | Expected `if` after `end`. |
| `FSIM-VHDL-PARSE-026` | error | Expected `;` after an `if` statement. |
| `FSIM-VHDL-PARSE-027` | error | Expected `;` after `null`. |
| `FSIM-VHDL-PARSE-028` | error | Expected `then` after an `if` condition. |
| `FSIM-VHDL-PARSE-029` | error | Expected `;` after an assignment. |
| `FSIM-VHDL-PARSE-031` | error | Expected `)` after a slice. |
| `FSIM-VHDL-PARSE-032` | error | Expected `)` after an index. |
| `FSIM-VHDL-PARSE-033` | error | Expected `)` after call arguments. |
| `FSIM-VHDL-PARSE-034` | error | Expected `)` after a parenthesized expression. |
| `FSIM-VHDL-PARSE-035` | error | Expected a VHDL expression. |
| `FSIM-VHDL-PARSE-036` | error | Expected `)` after an architecture name in an entity aspect. |
| `FSIM-VHDL-PARSE-037` | error | Expected `map` after `generic`. |
| `FSIM-VHDL-PARSE-038` | error | Expected `(` after `generic map`. |
| `FSIM-VHDL-PARSE-040` | error | Expected `map` after `port`. |
| `FSIM-VHDL-PARSE-041` | error | Expected `(` after `port map`. |
| `FSIM-VHDL-PARSE-042` | error | Expected `)` after port associations. |
| `FSIM-VHDL-PARSE-043` | error | Expected `;` after a VHDL instance. |
| `FSIM-VHDL-PARSE-044` | error | A VHDL library, use, or context-reference clause is malformed or unterminated. |
| `FSIM-VHDL-PARSE-046` | error | Expected `;` after an assertion. |
| `FSIM-VHDL-PARSE-047` | error | Expected `:` after process-variable names. |
| `FSIM-VHDL-PARSE-048` | error | Expected `;` after a process-variable declaration. |
| `FSIM-VHDL-PARSE-049` | error | Expected `;` after a wait statement. |
| `FSIM-VHDL-PARSE-050` | error | Expected `(` after an entity `generic` clause. |
| `FSIM-VHDL-PARSE-051` | error | Expected `:` after generic names. |
| `FSIM-VHDL-PARSE-052` | error | Expected `;` between generic declarations. |
| `FSIM-VHDL-PARSE-053` | error | Expected `)` after generic declarations. |
| `FSIM-VHDL-PARSE-054` | error | Expected `;` after a generic clause. |
| `FSIM-VHDL-PARSE-055` | error | Expected `)` after a generic map. |
| `FSIM-VHDL-PARSE-056` | error | Expected `generate` after a VHDL if-generate condition. |
| `FSIM-VHDL-PARSE-057` | error | Expected `generate` after `else` in a conditional generate. |
| `FSIM-VHDL-PARSE-058` | error | Expected `end` for a conditional generate. |
| `FSIM-VHDL-PARSE-059` | error | Expected `generate` after the conditional-generate `end`. |
| `FSIM-VHDL-PARSE-060` | error | A conditional-generate end label does not match its opening label. |
| `FSIM-VHDL-PARSE-061` | error | Expected `;` after a conditional generate. |
| `FSIM-VHDL-PARSE-062` | error | Expected `in` after a VHDL generate-loop variable. |
| `FSIM-VHDL-PARSE-063` | error | Expected `to` or `downto` in a VHDL generate iteration range. |
| `FSIM-VHDL-PARSE-064` | error | Expected `generate` after a VHDL generate iteration range. |
| `FSIM-VHDL-PARSE-065` | error | Expected `end` for an iterative generate. |
| `FSIM-VHDL-PARSE-066` | error | Expected `generate` after the iterative-generate `end`. |
| `FSIM-VHDL-PARSE-067` | error | An iterative-generate end label does not match its opening label. |
| `FSIM-VHDL-PARSE-068` | error | Expected `;` after an iterative generate. |
| `FSIM-VHDL-PARSE-069` | error | Expected `generate` after a VHDL case-generate selector. |
| `FSIM-VHDL-PARSE-070` | error | A case-generate alternative has no stable label. |
| `FSIM-VHDL-PARSE-071` | error | A case-generate alternative label or `when` introducer is malformed. |
| `FSIM-VHDL-PARSE-072` | error | A case-generate alternative has no choice. |
| `FSIM-VHDL-PARSE-073` | error | Expected `=>` after case-generate choices. |
| `FSIM-VHDL-PARSE-074` | error | Expected `end` for a case generate. |
| `FSIM-VHDL-PARSE-075` | error | Expected `generate` after the case-generate `end`. |
| `FSIM-VHDL-PARSE-076` | error | A case-generate end label does not match its opening label. |
| `FSIM-VHDL-PARSE-077` | error | Expected `;` after a case generate. |
| `FSIM-VHDL-PARSE-078` | error | Expected `begin` in a VHDL block statement. |
| `FSIM-VHDL-PARSE-079` | error | Expected `end` for a VHDL block statement. |
| `FSIM-VHDL-PARSE-080` | error | Expected `block` after the block-statement `end`. |
| `FSIM-VHDL-PARSE-081` | error | A block-statement end label does not match its opening label. |
| `FSIM-VHDL-PARSE-082` | error | Expected `;` after a VHDL block statement. |
| `FSIM-VHDL-PARSE-083` | error | Expected `:` after generated constant names. |
| `FSIM-VHDL-PARSE-084` | error | A generated constant lacks its required `:=` default expression. |
| `FSIM-VHDL-PARSE-085` | error | Expected `;` after a generated constant declaration. |
| `FSIM-VHDL-PARSE-086` | error | Expected `is` after a VHDL package name. |
| `FSIM-VHDL-PARSE-087` | error | Expected `:` after package constant names. |
| `FSIM-VHDL-PARSE-088` | error | A package constant lacks its required `:=` default expression. |
| `FSIM-VHDL-PARSE-089` | error | Expected `;` after a package constant declaration. |
| `FSIM-VHDL-PARSE-090` | error | Expected `is` after a VHDL context declaration name. |
| `FSIM-VHDL-PARSE-091` | error | Expected `end` for a VHDL context declaration. |
| `FSIM-VHDL-PARSE-092` | error | A context declaration end name does not match its opening name. |
| `FSIM-VHDL-PARSE-093` | error | Expected `;` after a VHDL context declaration. |
| `FSIM-VHDL-PARSE-094` | error | Expected `is` after a sequential VHDL case selector. |
| `FSIM-VHDL-PARSE-095` | error | Expected `when` before a sequential VHDL case alternative. |
| `FSIM-VHDL-PARSE-096` | error | Expected `=>` after sequential VHDL case choices. |
| `FSIM-VHDL-PARSE-097` | error | Expected `end` for a sequential VHDL case statement. |
| `FSIM-VHDL-PARSE-098` | error | Expected `case` after the sequential statement's `end`. |
| `FSIM-VHDL-PARSE-099` | error | Expected `;` after a sequential VHDL case statement. |
| `FSIM-VHDL-PARSE-100` | error | Expected `in` after a sequential VHDL for-loop parameter. |
| `FSIM-VHDL-PARSE-101` | error | Expected `to` or `downto` in a sequential VHDL for-loop range. |
| `FSIM-VHDL-PARSE-102` | error | Expected `loop` after a sequential VHDL for-loop range. |
| `FSIM-VHDL-PARSE-103` | error | Expected `end` for a sequential VHDL for loop. |
| `FSIM-VHDL-PARSE-104` | error | Expected `loop` after the sequential for-loop body. |
| `FSIM-VHDL-PARSE-105` | error | Expected `;` after a sequential VHDL for loop. |
| `FSIM-VHDL-PARSE-106` | error | Expected `loop` after a VHDL while condition. |
| `FSIM-VHDL-PARSE-107` | error | Expected `end` for a sequential VHDL while loop. |
| `FSIM-VHDL-PARSE-108` | error | Expected `loop` after a sequential VHDL while-loop body. |
| `FSIM-VHDL-PARSE-109` | error | Expected `;` after a sequential VHDL while loop. |
| `FSIM-VHDL-PARSE-110` | error | Expected `;` after a VHDL `exit` or `next` statement. |
| `FSIM-VHDL-PARSE-111` | error | Expected `end` after an unconditional VHDL sequential-loop body. |
| `FSIM-VHDL-PARSE-112` | error | Expected `loop` after `end` for an unconditional VHDL sequential loop. |
| `FSIM-VHDL-PARSE-113` | error | Expected `;` after an unconditional VHDL sequential loop. |
| `FSIM-VHDL-PARSE-114` | error | A VHDL exponent has an unparenthesized sign or an exponentiation chain omits required parentheses. |
| `FSIM-VHDL-PARSE-115` | error | A VHDL conditional assignment is missing its `else` alternative. |
| `FSIM-VHDL-PARSE-116` | error | Expected `select` after a VHDL selected-assignment selector. |
| `FSIM-VHDL-PARSE-117` | error | Expected `<=` before selected-assignment waveforms. |
| `FSIM-VHDL-PARSE-118` | error | Expected `when` after a selected-assignment waveform. |
| `FSIM-VHDL-PARSE-119` | error | Expected `;` after a selected signal assignment. |
| `FSIM-VHDL-PARSE-120` | error | Expected `)` after a VHDL array-attribute dimension. |
| `FSIM-VHDL-PARSE-122` | error | Expected `;` after a VHDL `report` statement. |
| `FSIM-VHDL-PARSE-123` | error | Expected `inertial` after a VHDL `reject` time. |
| `FSIM-VHDL-PARSE-124` | error | A VHDL delay mechanism appears anywhere other than immediately after `<=`. |
| `FSIM-VHDL-PARSE-125` | error | `unaffected` is mixed with other elements instead of forming a complete waveform alternative. |
| `FSIM-VHDL-PARSE-126` | error | A VHDL waveform is empty or lacks a value. |
| `FSIM-VHDL-PARSE-127` | error | Expected `is` in a VHDL type declaration. |
| `FSIM-VHDL-PARSE-128` | error | Expected `:` after VHDL record element names. |
| `FSIM-VHDL-PARSE-129` | error | Expected `;` after a VHDL record element declaration. |
| `FSIM-VHDL-PARSE-130` | error | A bounded VHDL record declaration has no supported elements. |
| `FSIM-VHDL-PARSE-131` | error | A VHDL record declaration has a malformed `end record` clause. |
| `FSIM-VHDL-PARSE-132` | error | An aggregate choice is not a record element, `others`, or a supported locally static integer expression or range. |
| `FSIM-VHDL-PARSE-133` | error | An aggregate association has no value or a trailing comma has no following association. |
| `FSIM-VHDL-PARSE-134` | error | A VHDL subtype declaration is missing `is`. |
| `FSIM-VHDL-PARSE-135` | error | A VHDL subtype declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-136` | error | A VHDL enumeration literal is neither an identifier nor a character literal. |
| `FSIM-VHDL-PARSE-137` | error | A VHDL enumeration declaration is missing a comma between literals. |
| `FSIM-VHDL-PARSE-138` | error | A VHDL enumeration declaration has a trailing comma. |
| `FSIM-VHDL-PARSE-139` | error | A VHDL enumeration declaration contains no valid literals. |
| `FSIM-VHDL-PARSE-140` | error | A VHDL enumeration declaration is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-141` | error | A VHDL enumeration declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-142` | error | A one-argument VHDL enumeration scalar attribute is missing its parenthesized argument. |
| `FSIM-VHDL-PARSE-143` | error | A VHDL array declaration is missing its opening parenthesis. |
| `FSIM-VHDL-PARSE-144` | error | An unconstrained VHDL array index has a malformed `<>` box. |
| `FSIM-VHDL-PARSE-145` | error | A constrained VHDL array index is missing `to` or `downto`. |
| `FSIM-VHDL-PARSE-146` | error | A VHDL array index definition is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-147` | error | A VHDL array declaration is missing `of` before its element subtype. |
| `FSIM-VHDL-PARSE-148` | error | A VHDL array type declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-149` | error | A VHDL aggregate range choice is missing its right bound. |
| `FSIM-VHDL-PARSE-150` | error | A VHDL aggregate choice list has no choice after `|`. |
| `FSIM-VHDL-PARSE-151` | error | A VHDL aggregate range or choice list is not followed by `=>`. |
| `FSIM-VHDL-PARSE-152` | error | A VHDL function formal declaration is missing `:` after its names. |
| `FSIM-VHDL-PARSE-153` | error | VHDL function formal declarations are not separated by `;`. |
| `FSIM-VHDL-PARSE-154` | error | A VHDL function formal list is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-155` | error | A VHDL function specification is missing `return`. |
| `FSIM-VHDL-PARSE-156` | error | An interface-function box default has a malformed `<>`. |
| `FSIM-VHDL-PARSE-157` | error | An interface function's `is` clause has neither a function name nor `<>`. |
| `FSIM-VHDL-PARSE-158` | error | A VHDL function body is missing `is`. |
| `FSIM-VHDL-PARSE-159` | error | A VHDL function-local variable declaration is missing `:`. |
| `FSIM-VHDL-PARSE-160` | error | A VHDL function-local variable declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-161` | error | A VHDL function body is missing `begin`. |
| `FSIM-VHDL-PARSE-162` | error | A VHDL function body is missing `end`. |
| `FSIM-VHDL-PARSE-163` | error | A VHDL function body is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-164` | error | A VHDL function return statement has no result expression. |
| `FSIM-VHDL-PARSE-165` | error | A VHDL return statement is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-166` | error | A bounded VHDL package body is missing `end`. |
| `FSIM-VHDL-PARSE-167` | error | A bounded VHDL package body is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-168` | error | A VHDL procedure parameter declaration is missing the colon before its mode and subtype. |
| `FSIM-VHDL-PARSE-169` | error | Adjacent VHDL procedure parameter declarations are missing a separating semicolon. |
| `FSIM-VHDL-PARSE-170` | error | A VHDL procedure parameter list is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-171` | error | An interface-procedure box default is missing the closing `>`. |
| `FSIM-VHDL-PARSE-172` | error | An interface-procedure `is` default is neither a procedure name nor `<>`. |
| `FSIM-VHDL-PARSE-173` | error | A VHDL procedure body is missing `is`. |
| `FSIM-VHDL-PARSE-174` | error | A VHDL procedure-local variable declaration is missing its colon. |
| `FSIM-VHDL-PARSE-175` | error | A VHDL procedure-local variable declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-176` | error | A VHDL procedure body is missing `begin`. |
| `FSIM-VHDL-PARSE-177` | error | A VHDL procedure body is missing `end`. |
| `FSIM-VHDL-PARSE-178` | error | A VHDL procedure body is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-179` | error | A VHDL procedure call is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-180` | error | A VHDL procedure call is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-181` | error | A package generic-map aspect is missing `map`. |
| `FSIM-VHDL-PARSE-182` | error | A package generic-map aspect is missing its opening parenthesis. |
| `FSIM-VHDL-PARSE-183` | error | A whole-map package generic box is missing its closing `>`. |
| `FSIM-VHDL-PARSE-184` | error | A whole-map package generic box is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-185` | error | A defaulted package generic association is missing its closing `>`. |
| `FSIM-VHDL-PARSE-186` | error | A package generic association list is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-187` | error | An interface package declaration is missing `is`. |
| `FSIM-VHDL-PARSE-188` | error | An interface package declaration is missing `new`. |
| `FSIM-VHDL-PARSE-189` | error | An interface package declaration is missing its `generic map` aspect. |
| `FSIM-VHDL-PARSE-190` | error | A local package instantiation is missing `is`. |
| `FSIM-VHDL-PARSE-191` | error | A local package instantiation is missing `new`. |
| `FSIM-VHDL-PARSE-192` | error | A local package instantiation is missing its `generic map` aspect. |
| `FSIM-VHDL-PARSE-193` | error | A local package instantiation is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-194` | error | A generic subprogram instantiation is missing `is`. |
| `FSIM-VHDL-PARSE-195` | error | A generic subprogram instantiation is missing `new`. |
| `FSIM-VHDL-PARSE-196` | error | A generic subprogram aspect is missing `map`. |
| `FSIM-VHDL-PARSE-197` | error | A generic subprogram map is missing its opening parenthesis. |
| `FSIM-VHDL-PARSE-198` | error | A whole-map generic subprogram box is missing its closing `>`. |
| `FSIM-VHDL-PARSE-199` | error | A whole-map generic subprogram box is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-200` | error | A defaulted generic subprogram association is missing its closing `>`. |
| `FSIM-VHDL-PARSE-201` | error | A generic subprogram association list is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-202` | error | A generic subprogram instantiation is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-203` | error | A generic clause intended for a subprogram is not followed by a function or procedure. |
| `FSIM-VHDL-PARSE-204` | error | A configuration declaration is missing `of`. |
| `FSIM-VHDL-PARSE-205` | error | A configuration declaration is missing `is`. |
| `FSIM-VHDL-PARSE-206` | error | A configuration declaration is missing its top architecture block introduced by `for`. |
| `FSIM-VHDL-PARSE-207` | error | A component configuration or specification is missing the colon after its instantiation list. |
| `FSIM-VHDL-PARSE-208` | error | A component configuration or specification is missing `use`. |
| `FSIM-VHDL-PARSE-209` | error | A bounded configuration binding indication is missing `entity`. |
| `FSIM-VHDL-PARSE-210` | error | A configured entity aspect has no parenthesized architecture name. |
| `FSIM-VHDL-PARSE-211` | error | A configuration port-map aspect is missing `map`. |
| `FSIM-VHDL-PARSE-212` | error | A configuration port-map aspect is missing its opening parenthesis. |
| `FSIM-VHDL-PARSE-213` | error | A configuration port-map aspect is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-214` | error | A configuration binding indication is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-215` | error | A component configuration is missing `end for`. |
| `FSIM-VHDL-PARSE-216` | error | A component configuration is missing the semicolon after `end for`. |
| `FSIM-VHDL-PARSE-217` | error | An architecture block configuration is missing `end for`. |
| `FSIM-VHDL-PARSE-218` | error | An architecture block configuration is missing the semicolon after `end for`. |
| `FSIM-VHDL-PARSE-219` | error | A configuration declaration is missing `end`. |
| `FSIM-VHDL-PARSE-220` | error | A configuration declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-221` | error | A component port clause is missing its opening parenthesis. |
| `FSIM-VHDL-PARSE-222` | error | A component port declaration is missing the colon after its names. |
| `FSIM-VHDL-PARSE-223` | error | A component port declaration is missing a supported mode. |
| `FSIM-VHDL-PARSE-224` | error | Component port declarations are not separated by semicolons. |
| `FSIM-VHDL-PARSE-225` | error | A component port clause is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-226` | error | A component port clause is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-227` | error | A component declaration is missing its end clause. |
| `FSIM-VHDL-PARSE-228` | error | A component declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-229` | error | An indexed generate block configuration is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-230` | error | A default-box generic association is missing its closing angle bracket. |
| `FSIM-VHDL-PARSE-231` | error | Expected `)` after a VHDL block guard expression. |
| `FSIM-VHDL-PARSE-232` | error | Expected `;` after a VHDL block generic-map aspect. |
| `FSIM-VHDL-PARSE-233` | error | Expected `;` after a VHDL block port-map aspect. |
| `FSIM-VHDL-PARSE-234` | error | Expected `begin` after a VHDL generate declarative part. |
| `FSIM-VHDL-PARSE-235` | error | Expected `function` after a `pure` or `impure` prefix in a VHDL generate declarative part. |
| `FSIM-VHDL-PARSE-236` | error | A bounded VHDL object alias is missing `is`, its target, or its terminating semicolon. |
| `FSIM-VHDL-PARSE-237` | error | A VHDL access type declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-238` | error | A VHDL physical type range is missing `to` or `downto`. |
| `FSIM-VHDL-PARSE-239` | error | A VHDL physical type declaration is missing `units`. |
| `FSIM-VHDL-PARSE-240` | error | A secondary physical unit is missing its physical-literal scale. |
| `FSIM-VHDL-PARSE-241` | error | A physical unit declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-242` | error | A VHDL physical type declaration is missing `end`. |
| `FSIM-VHDL-PARSE-243` | error | A VHDL physical type end clause is missing `units`. |
| `FSIM-VHDL-PARSE-244` | error | A VHDL physical type declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-245` | error | A protected private-variable declaration is missing the colon after its names. |
| `FSIM-VHDL-PARSE-246` | error | A protected private-variable declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-247` | error | A VHDL protected declaration or body is missing `end`. |
| `FSIM-VHDL-PARSE-248` | error | A VHDL protected end clause is missing `protected`. |
| `FSIM-VHDL-PARSE-249` | error | A VHDL protected-body end clause is missing `body`. |
| `FSIM-VHDL-PARSE-250` | error | A VHDL protected declaration or body is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-251` | error | A dereferenced index is missing its closing parenthesis, or a shared declaration is missing `variable`. |
| `FSIM-VHDL-PARSE-252` | error | A VHDL shared-variable declaration is missing the colon after its names. |
| `FSIM-VHDL-PARSE-253` | error | A VHDL shared-variable declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-254` | error | A VHDL file type declaration is missing `of` before its element subtype. |
| `FSIM-VHDL-PARSE-255` | error | A VHDL file type declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-256` | error | A VHDL file object declaration is missing the colon after its names. |
| `FSIM-VHDL-PARSE-257` | error | A VHDL file object `open` clause is missing its open-kind expression. |
| `FSIM-VHDL-PARSE-258` | error | A VHDL file object `is` clause is missing its logical-name expression. |
| `FSIM-VHDL-PARSE-259` | error | A VHDL file object open-kind expression is not followed by `is` and a logical name. |
| `FSIM-VHDL-PARSE-260` | error | A VHDL file object declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-261` | error | A VHDL report statement or assertion report clause is missing its expression. |
| `FSIM-VHDL-PARSE-262` | error | A VHDL severity clause is missing its expression. |
| `FSIM-VHDL-PARSE-263` | error | A VHDL bit-string literal has an invalid or separated base/width, malformed digits, lossy signed/unsigned adjustment, or a result that cannot be materialized in host-addressable storage. |
| `FSIM-VHDL-PARSE-264` | error | One or more VHDL context clauses at end of file are not followed by a library unit. |
| `FSIM-VHDL-PARSE-265` | error | A VHDL attribute declaration or specification has malformed form or termination. |
| `FSIM-VHDL-PARSE-266` | error | A VHDL attribute specification has an empty or missing entity-name list or entity-class separator. |
| `FSIM-VHDL-PARSE-267` | error | A VHDL attribute specification is missing `is` before its value. |
| `FSIM-VHDL-PARSE-268` | error | A VHDL group declaration has no template/instance separator or terminating semicolon. |
| `FSIM-VHDL-PARSE-269` | error | A VHDL group declaration has malformed, empty, or unterminated entries. |
| `FSIM-VHDL-PARSE-270` | error | A VHDL case expression is missing `is` after its selector. |
| `FSIM-VHDL-PARSE-271` | error | A VHDL case-expression alternative is missing `when`. |
| `FSIM-VHDL-PARSE-272` | error | `others` is combined with another case-expression choice. |
| `FSIM-VHDL-PARSE-273` | error | A VHDL case-expression choice list is missing `=>`. |
| `FSIM-VHDL-PARSE-274` | error | A VHDL case expression repeats `others` or places it before another alternative. |
| `FSIM-VHDL-PARSE-275` | error | A bounded VHDL case expression lacks `others` and does not contain exhaustive Boolean choices. |
| `FSIM-VHDL-PARSE-276` | error | A bounded VHDL external name uses an object class other than `signal`. |
| `FSIM-VHDL-PARSE-277` | error | A bounded VHDL external signal name is not a local or absolute dot-separated path. |
| `FSIM-VHDL-PARSE-278` | error | A VHDL external signal name is missing the colon before its subtype indication. |
| `FSIM-VHDL-PARSE-279` | error | A VHDL external signal name is missing its closing `>>`. |
| `FSIM-VHDL-PARSE-280` | error | A VHDL force assignment omits its forcing expression. |
| `FSIM-VHDL-PARSE-281` | error | A VHDL release assignment incorrectly includes an expression. |
| `FSIM-VHDL-PARSE-282` | error | A VHDL force or release assignment is missing its semicolon. |
| `FSIM-VHDL-PARSE-283` | error | A VHDL disconnection specification is missing the colon after its guarded-signal list. |
| `FSIM-VHDL-PARSE-284` | error | A VHDL disconnection specification is missing `after` before its time expression. |
| `FSIM-VHDL-PARSE-285` | error | A VHDL disconnection specification is missing its terminating semicolon. |

### Embedded VHDL PSL parsing and ownership

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-VHDL-PSL-001` | error | A PSL verification unit has no name, target, balanced target delimiters, braced body, or closing brace. |
| `FSIM-VHDL-PSL-002` | error | A PSL default-clock, Boolean, sequence, property, or endpoint declaration has a missing name, separator, body, or terminator. |
| `FSIM-VHDL-PSL-003` | error | A VHDL declarative region repeats a PSL declaration name. |
| `FSIM-VHDL-PSL-004` | error | A VHDL declarative region contains more than one PSL default-clock declaration. |
| `FSIM-VHDL-PSL-005` | error | A PSL formal-parameter list contains an empty, unnamed, duplicate, unbalanced, or unterminated formal. |
| `FSIM-VHDL-PSL-006` | error | A PSL assert, assume, restrict, or cover directive has no property or terminating semicolon. |
| `FSIM-VHDL-PSL-007` | error | A VHDL statement region repeats a PSL directive label. |
| `FSIM-VHDL-PSL-008` | error | A PSL verification unit contains an item outside the retained declaration and directive grammar. |
| `FSIM-VHDL-PSL-009` | error | A comment-embedded PSL item appears at top level or in a declarative region where that item kind is not legal. |
| `FSIM-VHDL-PSL-010` | error | A temporal PSL declaration or directive has no inferable default, referenced, or explicit clock. |
| `FSIM-VHDL-PSL-011` | error | A PSL clock expression does not sample a visible scalar Boolean, bit, or logic signal. |
| `FSIM-VHDL-PSL-012` | error | A PSL sampled name denotes a nonscalar object outside the Boolean, bit, or logic sampling domain. |
| `FSIM-VHDL-PSL-013` | error | A PSL declaration contains a temporal operator or declaration/formal reference that is incompatible with its Boolean, sequence, property, endpoint, or static-bound context. |
| `FSIM-VHDL-PSL-014` | error | A PSL next/repetition/recurrence bound is unbalanced, nonstatic, negative, reversed, or otherwise outside the bounded range grammar. |
| `FSIM-VHDL-PSL-015` | error | A PSL declaration references a declaration analyzed under an incompatible clock. |
| `FSIM-VHDL-PSL-016` | error | A PSL endpoint declaration does not denote a sequence expression. |
| `FSIM-VHDL-PSL-017` | error | A simple PSL declaration or sampled name cannot be resolved in its VHDL target scope. |
| `FSIM-VHDL-PSL-018` | error | A PSL explicit clock override has no expression or no clock expression. |
| `FSIM-VHDL-PSL-019` | error | PSL declarations form a cyclic reference graph. |

### VHDL semantics and bounded-subset rejections

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-VHDL-SEM-002` | error | Duplicate port declaration. |
| `FSIM-VHDL-SEM-003` | error | Duplicate signal declaration. |
| `FSIM-VHDL-SEM-012` | error | A process combines a sensitivity list with an explicit wait statement. |
| `FSIM-VHDL-SEM-013` | error | A generic name is declared more than once. |
| `FSIM-VHDL-SEM-014` | error | A generic conflicts with a port or signal in the same declarative namespace. |
| `FSIM-VHDL-SEM-015` | error | A named generic actual is repeated in one map. |
| `FSIM-VHDL-SEM-016` | error | A positional generic actual follows a named actual. |
| `FSIM-VHDL-SEM-017` | error | A case generate contains more than one `others` alternative. |
| `FSIM-VHDL-SEM-018` | error | An `others` case-generate alternative is not last. |
| `FSIM-VHDL-SEM-019` | error | A generated constant is duplicated or conflicts with a generated signal. |
| `FSIM-VHDL-SEM-020` | error | A package constant name is declared more than once. |
| `FSIM-VHDL-SEM-021` | error | A sequential VHDL case statement contains more than one `others` alternative. |
| `FSIM-VHDL-SEM-022` | error | A sequential VHDL case statement has an alternative after `others`. |
| `FSIM-VHDL-SEM-023` | error | A VHDL `exit` or `next` statement appears outside a sequential loop. |
| `FSIM-VHDL-SEM-024` | error | A VHDL `exit` or `next` targets a loop label that is not visible. |
| `FSIM-VHDL-SEM-025` | error | An `end loop` label is orphaned or does not match its opening label. |
| `FSIM-VHDL-SEM-026` | error | A VHDL loop label duplicates another sequential label in the enclosing process. |
| `FSIM-VHDL-SEM-027` | error | A selected assignment contains more than one `others` alternative. |
| `FSIM-VHDL-SEM-028` | error | A selected-assignment alternative follows `others`. |
| `FSIM-VHDL-SEM-029` | error | The bounded selected-assignment form has no final `others` alternative. |
| `FSIM-VHDL-SEM-030` | error | The selected VHDL attribute is outside the bounded supported array-attribute set. |
| `FSIM-VHDL-SEM-031` | error | A VHDL rejection limit is negative. |
| `FSIM-VHDL-SEM-032` | error | A VHDL rejection limit exceeds the first waveform-element delay. |
| `FSIM-VHDL-SEM-033` | error | A VHDL `reject` clause is paired with the transport delay mechanism. |
| `FSIM-VHDL-SEM-034` | error | VHDL waveform-element delays are not strictly ascending after exact project-time normalization. |
| `FSIM-VHDL-SEM-035` | error | A VHDL record declares the same case-insensitive element name more than once. |
| `FSIM-VHDL-SEM-036` | error | A VHDL design unit declares the same bounded type name more than once. |
| `FSIM-VHDL-SEM-037` | error | A VHDL record's optional end name does not match its declaration name. |
| `FSIM-VHDL-SEM-038` | error | A positional aggregate association follows a named association. |
| `FSIM-VHDL-SEM-039` | error | An aggregate contains multiple `others` associations or does not place `others` last. |
| `FSIM-VHDL-SEM-040` | error | A VHDL enumeration declares the same identifier or character literal more than once. |
| `FSIM-VHDL-SEM-041` | error | `others` is combined with another choice in one VHDL aggregate association. |
| `FSIM-VHDL-SEM-042` | error | A VHDL function profile declares the same formal name more than once. |
| `FSIM-VHDL-SEM-043` | error | A VHDL function declaration appears in a region that requires a body. |
| `FSIM-VHDL-SEM-044` | error | A VHDL function local duplicates the result, a formal, or another local. |
| `FSIM-VHDL-SEM-045` | error | A VHDL function end name does not match its designator. |
| `FSIM-VHDL-SEM-046` | error | A VHDL return statement appears outside a function body. |
| `FSIM-VHDL-SEM-047` | error | A package-body end name does not match its package name. |
| `FSIM-VHDL-SEM-048` | error | A bounded VHDL function body has no explicit return statement. |
| `FSIM-VHDL-SEM-049` | error | A constant-class VHDL procedure parameter has an output or inout mode. |
| `FSIM-VHDL-SEM-050` | error | A VHDL procedure profile declares the same formal name more than once. |
| `FSIM-VHDL-SEM-051` | error | A VHDL procedure declaration appears in a region that requires a body. |
| `FSIM-VHDL-SEM-052` | error | A VHDL procedure local duplicates a formal or another local. |
| `FSIM-VHDL-SEM-053` | error | A VHDL procedure end name does not match its designator. |
| `FSIM-VHDL-SEM-054` | error | A VHDL procedure return statement carries a value. |
| `FSIM-VHDL-SEM-055` | error | A positional VHDL procedure-call actual follows a named actual. |
| `FSIM-VHDL-SEM-056` | error | A bounded VHDL procedure body assigns to a constant-class formal. |
| `FSIM-VHDL-SEM-057` | error | A package generic-map aspect associates the same named formal more than once. |
| `FSIM-VHDL-SEM-058` | error | A positional package generic association follows a named association. |
| `FSIM-VHDL-SEM-059` | error | A declarative region contains the same local package-instance name more than once. |
| `FSIM-VHDL-SEM-060` | error | A generic subprogram map associates the same named formal more than once. |
| `FSIM-VHDL-SEM-061` | error | A positional generic subprogram association follows a named association. |
| `FSIM-VHDL-SEM-062` | error | A generic subprogram instance duplicates another subprogram declaration or instance. |
| `FSIM-VHDL-SEM-063` | error | A generic subprogram template has no generic formal. |
| `FSIM-VHDL-SEM-064` | error | A declarative region contains a duplicate generic function or procedure template. |
| `FSIM-VHDL-SEM-065` | error | A configuration instantiation list contains the same component label more than once. |
| `FSIM-VHDL-SEM-066` | error | `all` or `others` is combined with another configuration instantiation-list choice. |
| `FSIM-VHDL-SEM-067` | error | A configuration declaration's end name does not match its opening name. |
| `FSIM-VHDL-SEM-068` | error | One declarative region repeats an exact component profile. |
| `FSIM-VHDL-SEM-069` | error | A component declaration's end name does not match its opening name. |
| `FSIM-VHDL-SEM-071` | error | A component generic or port formal is declared more than once or conflicts with another component formal. |
| `FSIM-VHDL-SEM-072` | error | A component port default is declared on a non-input formal. |
| `FSIM-VHDL-SEM-073` | error | A positional VHDL function-call actual follows a named actual. |
| `FSIM-VHDL-SEM-074` | error | A VHDL procedure parameter default is declared on a non-input formal. |
| `FSIM-VHDL-SEM-075` | error | A VHDL entity port default is declared on a non-input formal. |
| `FSIM-VHDL-SEM-076` | error | A VHDL value generic is declared with a nonconstant object class. |
| `FSIM-VHDL-SEM-077` | error | A VHDL value generic is declared with a mode other than input. |
| `FSIM-VHDL-SEM-078` | error | A named port actual is repeated in one instance map. |
| `FSIM-VHDL-SEM-079` | error | A positional port actual follows a named actual. |
| `FSIM-VHDL-SEM-081` | error | A generated VHDL declarative item conflicts with an earlier declaration from a different non-overloadable family. |
| `FSIM-VHDL-SEM-082` | error | A process or subprogram local declarative item conflicts with an earlier declaration from a different non-overloadable family. |
| `FSIM-VHDL-SEM-083` | error | A bounded VHDL declarative region repeats an object alias name. |
| `FSIM-VHDL-SEM-084` | error | A sequential `if`/`case` or process end label is orphaned or does not match its opening label. |
| `FSIM-VHDL-SEM-085` | error | A `process(all)` sensitivity clause also contains an explicit sensitivity name. |
| `FSIM-VHDL-SEM-086` | error | A VHDL matching case does not use `?` consistently after its opening and ending `case` keywords. |
| `FSIM-VHDL-SEM-087` | error | The `guarded` keyword appears on a sequential rather than concurrent signal assignment. |
| `FSIM-VHDL-SEM-088` | error | A VHDL physical type repeats a unit name. |
| `FSIM-VHDL-SEM-089` | error | A VHDL physical type declares no primary unit. |
| `FSIM-VHDL-SEM-090` | error | A VHDL physical type end name does not match its declaration name. |
| `FSIM-VHDL-SEM-091` | error | A VHDL protected body repeats a private-variable name. |
| `FSIM-VHDL-SEM-092` | error | A VHDL protected declaration or body end name does not match its declaration name. |
| `FSIM-VHDL-SEM-093` | error | A VHDL declarative region repeats a shared-variable name. |
| `FSIM-VHDL-SEM-094` | error | A VHDL declarative region repeats a file-object name. |
| `FSIM-VHDL-SEM-095` | error | A VHDL file interface declaration incorrectly specifies a parameter mode. |
| `FSIM-VHDL-SEM-096` | error | A VHDL file interface declaration incorrectly specifies a default expression. |
| `FSIM-VHDL-SEM-097` | error | An incomplete VHDL type has no full declaration in the same declarative region. |
| `FSIM-VHDL-SEM-098` | error | A VHDL declarative region repeats an attribute declaration name. |
| `FSIM-VHDL-SEM-099` | error | A VHDL attribute specification references an attribute not declared earlier in the region. |
| `FSIM-VHDL-SEM-100` | error | A VHDL declarative region repeats a group declaration name. |
| `FSIM-VHDL-SEM-101` | error | A VHDL group instance references a template not declared earlier in the region. |
| `FSIM-VHDL-SEM-102` | error | A VHDL force or release assignment appears outside a sequential statement region. |
| `FSIM-VHDL-SEM-103` | error | An ordinary VHDL process uses `postponed` in its closing clause. |
| `FSIM-VHDL-SEM-104` | error | A VHDL `postponed` prefix appears on a concurrent statement other than a process, assertion, or procedure call. |
| `FSIM-VHDL-SEM-105` | error | A VHDL disconnection specification's type mark does not match an explicitly selected signal. |
| `FSIM-VHDL-SEM-106` | error | VHDL disconnection specifications overlap or repeat one guarded signal. |
| `FSIM-VHDL-UNSUPPORTED-001` | error | Unsupported design unit or context item. |
| `FSIM-VHDL-UNSUPPORTED-003` | error | Unsupported entity declaration. |
| `FSIM-VHDL-UNSUPPORTED-004` | error | Unsupported architecture declaration. |
| `FSIM-VHDL-UNSUPPORTED-005` | error | Unsupported labeled concurrent statement. |
| `FSIM-VHDL-UNSUPPORTED-006` | error | Unsupported concurrent statement. |
| `FSIM-VHDL-UNSUPPORTED-007` | error | A process declarative item is outside the bounded constant, type/subtype, variable, alias, package-instance, and local-callable subset. |
| `FSIM-VHDL-UNSUPPORTED-008` | error | Unsupported sequential statement. |
| `FSIM-VHDL-UNSUPPORTED-012` | error | Signal initializers are parsed but not executable. |
| `FSIM-VHDL-UNSUPPORTED-014` | error | An integer-family subtype appears in a declaration context that does not yet admit scalar integer objects. |
| `FSIM-VHDL-UNSUPPORTED-015` | error | A nested context declaration appears where only a context reference is permitted. |
| `FSIM-VHDL-UNSUPPORTED-018` | error | A generic type is outside the bounded scalar integer, Boolean, bit, and physical-time subset. |
| `FSIM-VHDL-UNSUPPORTED-020` | error | A generate branch contains an item outside the bounded constant, local-signal, assignment, process, instance, and nested-generate subset. |
| `FSIM-VHDL-UNSUPPORTED-022` | error | A package declaration item is outside the bounded constant, type, subtype, or function subset. |
| `FSIM-VHDL-UNSUPPORTED-024` | error | A context declaration contains an item other than a library clause, use clause, or context reference. |
| `FSIM-VHDL-UNSUPPORTED-025` | error | A `null` waveform element appears outside a guarded concurrent signal assignment. |
| `FSIM-VHDL-UNSUPPORTED-026` | error | A bounded VHDL type or record element is outside the architecture-local, non-nested packed record subset. |
| `FSIM-VHDL-UNSUPPORTED-027` | error | A VHDL array declaration uses an index subtype outside the retained `integer`, `natural`, or `positive` subset, or applies multiple constraints to a built-in scalar/vector subtype. |
| `FSIM-VHDL-UNSUPPORTED-028` | error | A VHDL interface type generic uses classified or default-like syntax outside the VHDL-2008 unclassified `type T` form. |
| `FSIM-VHDL-UNSUPPORTED-029` | error | A bounded VHDL function formal is not constant class. |
| `FSIM-VHDL-UNSUPPORTED-030` | error | A bounded VHDL function formal is not input mode. |
| `FSIM-VHDL-UNSUPPORTED-031` | error | A VHDL function formal type is outside the bounded scalar integral or visible scalar-subtype profile. |
| `FSIM-VHDL-UNSUPPORTED-033` | error | A VHDL interface function uses an operator-symbol designator. |
| `FSIM-VHDL-UNSUPPORTED-034` | error | A VHDL function result type is outside the bounded scalar integral or visible scalar-subtype profile. |
| `FSIM-VHDL-UNSUPPORTED-035` | error | A VHDL function declarative item is outside the bounded local constant, type/subtype, variable, alias, package-instance, and callable subset. |
| `FSIM-VHDL-UNSUPPORTED-037` | error | A bounded VHDL function body contains timing, signal updates, or another unsupported statement. |
| `FSIM-VHDL-UNSUPPORTED-038` | error | A bounded VHDL procedure formal has signal or file class rather than constant or variable class. |
| `FSIM-VHDL-UNSUPPORTED-039` | error | A bounded VHDL procedure formal uses buffer, linkage, or another unsupported mode. |
| `FSIM-VHDL-UNSUPPORTED-040` | error | A VHDL procedure formal type is outside the bounded scalar integral or visible scalar-subtype profile. |
| `FSIM-VHDL-UNSUPPORTED-042` | error | A VHDL procedure uses an operator-symbol designator. |
| `FSIM-VHDL-UNSUPPORTED-043` | error | A VHDL procedure declarative item is outside the bounded local constant, type/subtype, variable, alias, package-instance, and callable subset. |
| `FSIM-VHDL-UNSUPPORTED-044` | error | A bounded VHDL procedure body contains a signal update or another unsupported statement other than a wait. |
| `FSIM-VHDL-UNSUPPORTED-045` | error | A nested interface-package formal appears in a bounded generic subprogram template. |
| `FSIM-VHDL-UNSUPPORTED-046` | error | A generic function instantiation carries a `pure` or `impure` prefix. |
| `FSIM-VHDL-UNSUPPORTED-047` | error | A bounded generic function template is impure. |
| `FSIM-VHDL-UNSUPPORTED-050` | error | A configuration declaration contains an item outside the bounded architecture/component configuration subset. |
| `FSIM-VHDL-UNSUPPORTED-052` | error | A component declaration contains an unsupported declarative item. |
| `FSIM-VHDL-UNSUPPORTED-053` | error | A generated VHDL declarative region contains an item outside the bounded constant, signal, alias, type, subtype, callable, component, and local-package subset. |
| `FSIM-VHDL-UNSUPPORTED-054` | error | A bounded VHDL object alias omits its explicit subtype indication. |
| `FSIM-VHDL-UNSUPPORTED-055` | error | A protected type contains a declarative item outside the bounded private-variable, function, and procedure subset. |

## Verilog and SystemVerilog frontend

### Verilog/SystemVerilog preprocessing

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-SV-PP-001` | error | The Verilog preprocessor was invoked for a non-Verilog language. |
| `FSIM-SV-PP-002` | error | A conditional compilation block is unterminated. |
| `FSIM-SV-PP-003` | error | A command-line or manifest macro definition has an invalid identifier. |
| `FSIM-SV-PP-004` | error | Recursive include processing exceeded the configured depth limit. |
| `FSIM-SV-PP-005` | error | An include cycle was detected. |
| `FSIM-SV-PP-006` | error | `` `elsif`` has no matching conditional opener. |
| `FSIM-SV-PP-007` | error | `` `elsif`` follows `` `else`` in the same conditional block. |
| `FSIM-SV-PP-008` | error | `` `else`` has no matching conditional opener. |
| `FSIM-SV-PP-009` | error | A conditional block contains more than one `` `else``. |
| `FSIM-SV-PP-010` | error | `` `endif`` has no matching conditional opener. |
| `FSIM-SV-PP-011` | error | A `protect` pragma is malformed, nested, unmatched, or contains encrypted payload that requires an unavailable decryption provider. |
| `FSIM-SV-PP-012` | error | A conditional or undefinition directive lacks its required macro identifier. |
| `FSIM-SV-PP-013` | error | Unexpected tokens follow a directive macro identifier. |
| `FSIM-SV-PP-014` | error | A no-argument conditional directive has trailing tokens. |
| `FSIM-SV-PP-015` | error | `` `define`` lacks a macro identifier. |
| `FSIM-SV-PP-016` | error | A function-like macro parameter list is malformed. |
| `FSIM-SV-PP-017` | error | A function-like macro repeats a parameter name. |
| `FSIM-SV-PP-018` | error | A function-like macro parameter list is unterminated. |
| `FSIM-SV-PP-019` | error | `` `include`` lacks a file name. |
| `FSIM-SV-PP-020` | error | A macro-expanded include name produces no tokens. |
| `FSIM-SV-PP-021` | error | An include name is neither a string literal nor an angle-bracket name. |
| `FSIM-SV-PP-022` | error | An include file cannot be resolved from the including file or configured search roots. |
| `FSIM-SV-PP-023` | error | A function-like macro invocation lacks an argument list. |
| `FSIM-SV-PP-024` | error | A function-like macro argument list is unterminated. |
| `FSIM-SV-PP-025` | error | Token concatenation lacks an operand. |
| `FSIM-SV-PP-026` | error | Token concatenation does not form exactly one valid token. |
| `FSIM-SV-PP-027` | error | A backtick is not followed by a macro identifier. |
| `FSIM-SV-PP-028` | error | A macro invocation or compiler directive is undefined. |
| `FSIM-SV-PP-029` | error | Macro expansion is recursive or exceeds the configured depth limit. |
| `FSIM-SV-PP-030` | error | A function-like macro receives the wrong number of arguments. |
| `FSIM-SV-PP-031` | error | A Verilog preprocessing compilation unit contains no root files. |
| `FSIM-SV-PP-032` | error | A no-argument state directive has trailing tokens. |
| `FSIM-SV-PP-033` | error | `` `endcelldefine`` has no active `` `celldefine`` state. |
| `FSIM-SV-PP-034` | error | `` `default_nettype`` has a missing, invalid, or trailing value. |
| `FSIM-SV-PP-035` | error | `` `unconnected_drive`` does not contain exactly `pull0` or `pull1`. |
| `FSIM-SV-PP-036` | error | `` `begin_keywords`` has a missing, unsupported, or trailing IEEE version string. |
| `FSIM-SV-PP-037` | error | `` `end_keywords`` has no matching `` `begin_keywords``. |
| `FSIM-SV-PP-038` | error | A `` `begin_keywords`` region is unterminated. |
| `FSIM-SV-PP-039` | error | A `` `line`` directive does not contain exactly a line number, quoted file name, and level. |
| `FSIM-SV-PP-040` | error | A `` `line`` line number is not a positive representable decimal integer. |
| `FSIM-SV-PP-041` | error | A `` `line`` logical file name is not a string literal. |
| `FSIM-SV-PP-042` | error | A `` `line`` logical file name contains an invalid escape. |
| `FSIM-SV-PP-043` | error | A `` `line`` level is not `0`, `1`, or `2`. |
| `FSIM-SV-PP-044` | error | A macro is redefined with a different parameter list, defaults, or replacement. |
| `FSIM-SV-PP-045` | error | Reserved legacy diagnostic; textual include semantics no longer emit an include-end conditional restriction. |
| `FSIM-SV-PP-046` | error | Reserved legacy diagnostic; textual include semantics no longer emit a cross-file conditional restriction. |
| `FSIM-SV-PP-047` | error | `` `celldefine`` is nested or repeated while already active. |
| `FSIM-SV-PP-048` | error | `` `nounconnected_drive`` appears without active `` `unconnected_drive`` state. |
| `FSIM-SV-PP-049` | error | A conditional directive embedded in a macro replacement is malformed or unmatched. |
| `FSIM-SV-PP-050` | error | A macro replacement contains an unterminated conditional directive. |
| `FSIM-SV-PP-051` | error | A special macro string contains a backtick that does not introduce an identifier. |
| `FSIM-SV-PP-052` | error | A compiler directive, macro form, lexical token, or keyword region requires a later Verilog/SystemVerilog revision, or the selected revision belongs to the wrong language family. |

### Verilog/SystemVerilog syntax

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-SV-PARSE-001` | error | Expected a Verilog/SystemVerilog identifier. |
| `FSIM-SV-PARSE-002` | error | Expected `)` after module ports. |
| `FSIM-SV-PARSE-003` | error | Expected `;` after a module header. |
| `FSIM-SV-PARSE-004` | error | Expected `endmodule`. |
| `FSIM-SV-PARSE-005` | error | Expected `:` in a packed range. |
| `FSIM-SV-PARSE-006` | error | Expected `]` after a packed range. |
| `FSIM-SV-PARSE-008` | error | Expected `;` after a declaration. |
| `FSIM-SV-PARSE-009` | error | Expected a continuous-assignment target. |
| `FSIM-SV-PARSE-010` | error | Expected `=` in a continuous assignment. |
| `FSIM-SV-PARSE-011` | error | Expected `;` after a continuous assignment. |
| `FSIM-SV-PARSE-012` | error | An `always` process lacks the required event control for this slice. |
| `FSIM-SV-PARSE-013` | error | Expected `initial`. |
| `FSIM-SV-PARSE-014` | error | Expected `(` after `@`. |
| `FSIM-SV-PARSE-015` | error | Expected `)` after `@*`. |
| `FSIM-SV-PARSE-016` | error | Expected `)` after a sensitivity list. |
| `FSIM-SV-PARSE-017` | error | Expected `end` after a procedural block. |
| `FSIM-SV-PARSE-018` | error | Expected `(` after `if`. |
| `FSIM-SV-PARSE-019` | error | Expected `)` after an `if` condition. |
| `FSIM-SV-PARSE-020` | error | Expected `)` after `$finish` arguments. |
| `FSIM-SV-PARSE-021` | error | Expected `;` after `$finish`. |
| `FSIM-SV-PARSE-022` | error | Expected `;` after a procedural assignment. |
| `FSIM-SV-PARSE-023` | error | Expected a delay magnitude. |
| `FSIM-SV-PARSE-024` | error | Expected `)` after a parenthesized delay. |
| `FSIM-SV-PARSE-025` | error | Expected `]` after an assignment-target part-select. |
| `FSIM-SV-PARSE-026` | error | Expected `]` after an assignment-target index. |
| `FSIM-SV-PARSE-027` | error | Expected `:` in a conditional expression. |
| `FSIM-SV-PARSE-028` | error | Expected `)` after call arguments. |
| `FSIM-SV-PARSE-029` | error | Expected `)` after a parenthesized expression. |
| `FSIM-SV-PARSE-030` | error | Expected `}` after a concatenation. |
| `FSIM-SV-PARSE-031` | error | Expected an expression. |
| `FSIM-SV-PARSE-032` | error | Expected `]` after an expression part-select. |
| `FSIM-SV-PARSE-033` | error | Expected `]` after an expression index. |
| `FSIM-SV-PARSE-034` | error | Expected `(` after an instance name. |
| `FSIM-SV-PARSE-035` | error | Expected `(` after a named port. |
| `FSIM-SV-PARSE-036` | error | Expected `)` after a named port connection. |
| `FSIM-SV-PARSE-037` | error | Expected `)` after instance connections. |
| `FSIM-SV-PARSE-038` | error | Expected `;` after a module instance. |
| `FSIM-SV-PARSE-137` | error | Expected `(` after `assert`. |
| `FSIM-SV-PARSE-138` | error | Expected `)` after an assertion condition. |
| `FSIM-SV-PARSE-139` | error | Expected `;` after a SystemVerilog function return statement. |
| `FSIM-SV-PARSE-140` | error | Expected `)` after a bounded SystemVerilog function argument list. |
| `FSIM-SV-PARSE-141` | error | Expected `;` after a bounded SystemVerilog function header. |
| `FSIM-SV-PARSE-142` | error | Expected `endfunction` after a bounded SystemVerilog function body. |
| `FSIM-SV-PARSE-143` | error | Expected `)` after a bounded SystemVerilog task argument list. |
| `FSIM-SV-PARSE-144` | error | Expected `;` after a bounded SystemVerilog task header. |
| `FSIM-SV-PARSE-145` | error | Expected `endtask` after a bounded SystemVerilog task body. |
| `FSIM-SV-PARSE-146` | error | Expected `)` after a bounded SystemVerilog task call argument list. |
| `FSIM-SV-PARSE-147` | error | Expected `;` after a bounded SystemVerilog task call. |
| `FSIM-SV-PARSE-148` | error | Expected `(` after `$fclose`. |
| `FSIM-SV-PARSE-149` | error | Expected `)` after a `$fclose` handle. |
| `FSIM-SV-PARSE-150` | error | Expected `;` after `$fclose`. |
| `FSIM-SV-PARSE-151` | error | Expected `(` after `$fdisplay`, `$fwrite`, `$fstrobe`, or `$fmonitor`. |
| `FSIM-SV-PARSE-152` | error | Expected `,` after a file-output handle. |
| `FSIM-SV-PARSE-153` | error | Expected `)` after file-output arguments. |
| `FSIM-SV-PARSE-154` | error | Expected `;` after `$fdisplay`, `$fwrite`, `$fstrobe`, or `$fmonitor`. |
| `FSIM-SV-PARSE-155` | error | Expected `]` after a SystemVerilog queue dimension. |
| `FSIM-SV-PARSE-156` | error | Expected `;` after a supported container method call. |
| `FSIM-SV-PARSE-157` | error | Expected `]` after a SystemVerilog associative-array index type. |
| `FSIM-SV-PARSE-158` | error | Expected `]` after a SystemVerilog static unpacked-array range. |
| `FSIM-SV-PARSE-159` | error | Expected `(` after a `$readmem*` or `$writemem*` task name. |
| `FSIM-SV-PARSE-160` | error | Expected `,` after a memory-file task file name. |
| `FSIM-SV-PARSE-161` | error | Expected `)` after memory-file task arguments. |
| `FSIM-SV-PARSE-162` | error | Expected `;` after a memory-file task. |
| `FSIM-SV-PARSE-163` | error | Expected `{` after a SystemVerilog assignment-pattern apostrophe. |
| `FSIM-SV-PARSE-164` | error | Expected `}` after a SystemVerilog assignment pattern. |
| `FSIM-SV-PARSE-165` | error | Expected `(` after a predicate container locator's `with` keyword. |
| `FSIM-SV-PARSE-166` | error | Expected `)` after a predicate container locator expression. |
| `FSIM-SV-PARSE-167` | error | Expected `(` after a container reduction's `with` keyword. |
| `FSIM-SV-PARSE-168` | error | Expected `)` after a container reduction transformation. |
| `FSIM-SV-PARSE-169` | error | Expected `(` after a container ordering method's `with` keyword. |
| `FSIM-SV-PARSE-170` | error | Expected `)` after a container ordering key expression. |
| `FSIM-SV-PARSE-171` | error | Expected `(` after an extrema/uniqueness locator's transformation `with` keyword. |
| `FSIM-SV-PARSE-172` | error | Expected `)` after an extrema/uniqueness locator transformation. |
| `FSIM-SV-PARSE-173` | error | Expected `:` after a SystemVerilog assignment-pattern `default` choice. |
| `FSIM-SV-PARSE-174` | error | Expected a value after a SystemVerilog assignment-pattern association. |
| `FSIM-SV-PARSE-175` | error | The `inside` membership operator is used outside SystemVerilog. |
| `FSIM-SV-PARSE-176` | error | Expected the braced list after an `inside` membership operator. |
| `FSIM-SV-PARSE-177` | error | An `inside` membership list is empty. |
| `FSIM-SV-PARSE-178` | error | Expected `:` between the low and high bounds of an `inside` range. |
| `FSIM-SV-PARSE-179` | error | Expected `]` after an `inside` range. |
| `FSIM-SV-PARSE-180` | error | A `case inside` statement is used outside SystemVerilog. |
| `FSIM-SV-PARSE-181` | error | Expected `:` between the low and high bounds of a `case inside` range. |
| `FSIM-SV-PARSE-182` | error | Expected `]` after a `case inside` range. |
| `FSIM-SV-PARSE-183` | error | A `case inside` alternative contains no choice or a trailing empty choice. |
| `FSIM-SV-PARSE-184` | error | `inside` matching is combined with `casez` or `casex`. |
| `FSIM-SV-PARSE-185` | error | A `unique`, `unique0`, or `priority` case qualifier is used outside SystemVerilog. |
| `FSIM-SV-PARSE-186` | error | A case statement contains more than one qualifier. |
| `FSIM-SV-PARSE-187` | error | A `case matches` statement is used outside SystemVerilog. |
| `FSIM-SV-PARSE-188` | error | Bounded `matches` pattern matching is combined with `casez` or `casex`. |
| `FSIM-SV-PARSE-189` | error | A bounded `case matches` item contains a comma-separated pattern list instead of exactly one pattern. |
| `FSIM-SV-PARSE-190` | error | A `case matches` dot pattern is missing its wildcard or variable name. |
| `FSIM-SV-PARSE-191` | error | Expected the inner `{` before streaming-concatenation operands. |
| `FSIM-SV-PARSE-192` | error | A streaming concatenation contains no operand. |
| `FSIM-SV-PARSE-193` | error | Expected the inner `}` after streaming-concatenation operands. |
| `FSIM-SV-PARSE-194` | error | Expected the outer `}` after a streaming concatenation. |
| `FSIM-SV-PARSE-195` | error | Expected `=` in a procedural force statement. |
| `FSIM-SV-PARSE-196` | error | Expected `;` after a procedural force or release statement. |
| `FSIM-SV-PARSE-197` | error | Expected `;` after a classic function formal declaration. |
| `FSIM-SV-PARSE-198` | error | Expected `;` after a classic task formal declaration. |
| `FSIM-SV-PARSE-199` | error | Expected `(` after a named function actual. |
| `FSIM-SV-PARSE-200` | error | Expected `)` after a named function actual. |
| `FSIM-SV-PARSE-201` | error | Expected `(` after a named task actual. |
| `FSIM-SV-PARSE-202` | error | Expected `)` after a named task actual. |
| `FSIM-SV-PARSE-203` | error | A repeated intra-assignment event control is missing its opening parenthesis. |
| `FSIM-SV-PARSE-204` | error | A repeated intra-assignment event control is missing its closing parenthesis. |
| `FSIM-SV-PARSE-205` | error | A repeated intra-assignment event control is missing its `@` event marker. |
| `FSIM-SV-PARSE-206` | error | A procedural `fork` block is missing its terminating `join`, `join_any`, or `join_none`. |
| `FSIM-SV-PARSE-207` | error | A `wait fork` statement is missing its terminating semicolon. |
| `FSIM-SV-PARSE-209` | error | A `disable fork` statement is missing its terminating semicolon. |
| `FSIM-SV-PARSE-210` | error | A static gate-instance array range is missing its colon. |
| `FSIM-SV-PARSE-211` | error | A static gate-instance array range is missing its closing bracket. |
| `FSIM-SV-PARSE-212` | error | A modport declaration is missing the opening parenthesis after its name. |
| `FSIM-SV-PARSE-213` | error | A modport declaration is missing its closing parenthesis. |
| `FSIM-SV-PARSE-214` | error | A modport declaration is missing its terminating semicolon. |
| `FSIM-SV-PARSE-215` | error | A package export item is missing `::` after its package selector. |
| `FSIM-SV-PARSE-216` | error | A package export declaration is missing its terminating semicolon. |
| `FSIM-SV-PARSE-217` | error | A static module/interface instance array range is missing its colon. |
| `FSIM-SV-PARSE-218` | error | A static module/interface instance array range is missing its closing bracket. |
| `FSIM-SV-PARSE-219` | error | Expected `(` after a SystemVerilog cast type. |
| `FSIM-SV-PARSE-220` | error | Expected `)` after a SystemVerilog cast expression. |
| `FSIM-SV-PARSE-221` | error | Expected `:` in a multidimensional static unpacked range. |
| `FSIM-SV-PARSE-222` | error | Expected `]` after a multidimensional static unpacked range. |
| `FSIM-SV-PARSE-223` | error | Expected `)` after a dynamic-array `new[size](initializer)` expression. |
| `FSIM-SV-PARSE-224` | error | Expected `(` after a user-defined primitive name. |
| `FSIM-SV-PARSE-225` | error | Expected `)` after a user-defined primitive terminal list. |
| `FSIM-SV-PARSE-226` | error | Expected `;` after a user-defined primitive header. |
| `FSIM-SV-PARSE-227` | error | Expected `;` after a user-defined primitive terminal declaration. |
| `FSIM-SV-PARSE-228` | error | Expected `)` after a user-defined primitive transition pair. |
| `FSIM-SV-PARSE-229` | error | A user-defined primitive transition pair does not contain exactly two valid level symbols. |
| `FSIM-SV-PARSE-230` | error | A user-defined primitive table contains an invalid input symbol. |
| `FSIM-SV-PARSE-231` | error | Expected `:` after a user-defined primitive row's input symbols. |
| `FSIM-SV-PARSE-232` | error | A sequential user-defined primitive row has no current-state symbol. |
| `FSIM-SV-PARSE-233` | error | Expected `:` after a sequential user-defined primitive current-state symbol. |
| `FSIM-SV-PARSE-234` | error | A user-defined primitive row contains an invalid output symbol. |
| `FSIM-SV-PARSE-235` | error | Expected `;` after a user-defined primitive table row. |
| `FSIM-SV-PARSE-236` | error | Expected `;` after a sequential user-defined primitive output refinement. |
| `FSIM-SV-PARSE-237` | error | Expected `=` in a user-defined primitive initial statement. |
| `FSIM-SV-PARSE-238` | error | A user-defined primitive initial statement has no scalar value. |
| `FSIM-SV-PARSE-239` | error | Expected `;` after a user-defined primitive initial statement. |
| `FSIM-SV-PARSE-240` | error | A user-defined primitive contains an unsupported declaration item. |
| `FSIM-SV-PARSE-241` | error | Expected `table` in a user-defined primitive declaration. |
| `FSIM-SV-PARSE-242` | error | Expected `endtable` after user-defined primitive rows. |
| `FSIM-SV-PARSE-243` | error | Expected `endprimitive` after a user-defined primitive table. |
| `FSIM-SV-PARSE-244` | error | Expected a comma between Verilog drive-strength members. |
| `FSIM-SV-PARSE-245` | error | Expected a legal zero/one drive-strength member. |
| `FSIM-SV-PARSE-246` | error | Expected a closing parenthesis after a Verilog drive-strength pair. |
| `FSIM-SV-PARSE-247` | error | Expected a closing parenthesis after a Verilog charge strength. |
| `FSIM-SV-PARSE-248` | error | Expected an opening parenthesis before switch-primitive terminals. |
| `FSIM-SV-PARSE-249` | error | Expected a closing parenthesis after switch-primitive terminals. |
| `FSIM-SV-PARSE-250` | error | Expected a semicolon after a switch-primitive declaration. |
| `FSIM-SV-PARSE-251` | error | Expected a closing parenthesis after a single pull strength. |
| `FSIM-SV-PARSE-252` | error | Expected a colon in a switch-instance array range. |
| `FSIM-SV-PARSE-253` | error | Expected a closing bracket after a switch-instance array range. |
| `FSIM-SV-PARSE-254` | error | A `virtual` class qualifier does not introduce a class declaration or method. |
| `FSIM-SV-PARSE-255` | error | An `interface` class qualifier does not introduce an interface class declaration. |
| `FSIM-SV-PARSE-256` | error | A class forward declaration omits the `class` keyword. |
| `FSIM-SV-PARSE-257` | error | A class forward declaration omits its terminating semicolon. |
| `FSIM-SV-PARSE-258` | error | A class header omits its terminating semicolon. |
| `FSIM-SV-PARSE-259` | error | A class declaration omits `endclass`. |
| `FSIM-SV-PARSE-260` | error | Class property qualifiers are not followed by a data type. |
| `FSIM-SV-PARSE-261` | error | A class property declaration omits its terminating semicolon. |
| `FSIM-SV-PARSE-262` | error | A qualified class method declaration omits `function` or `task`. |
| `FSIM-SV-PARSE-263` | error | A pure or extern class constraint prototype omits its terminating semicolon. |
| `FSIM-SV-PARSE-264` | error | A class constraint body omits its opening brace. |
| `FSIM-SV-PARSE-265` | error | A class constraint expression omits its terminating semicolon. |
| `FSIM-SV-PARSE-266` | error | A class constraint body omits its closing brace. |
| `FSIM-SV-PARSE-267` | error | A qualified class constraint declaration omits the `constraint` keyword. |
| `FSIM-SV-PARSE-268` | error | A constraint `dist` list omits its opening brace. |
| `FSIM-SV-PARSE-269` | error | A constraint `dist` range omits its separating colon. |
| `FSIM-SV-PARSE-270` | error | A constraint `dist` range omits its closing bracket. |
| `FSIM-SV-PARSE-271` | error | A constraint `dist :/` weight omits its slash. |
| `FSIM-SV-PARSE-272` | error | A constraint distribution item uses neither `:=` nor `:/`. |
| `FSIM-SV-PARSE-273` | error | A constraint `dist` list omits its closing brace. |
| `FSIM-SV-PARSE-274` | error | A structured constraint set omits its closing brace. |
| `FSIM-SV-PARSE-275` | error | A conditional constraint omits the opening parenthesis. |
| `FSIM-SV-PARSE-276` | error | A conditional constraint omits the closing parenthesis. |
| `FSIM-SV-PARSE-277` | error | A constraint `foreach` selection omits the opening parenthesis. |
| `FSIM-SV-PARSE-278` | error | A constraint `foreach` selection omits the closing parenthesis. |
| `FSIM-SV-PARSE-279` | error | A solve-order constraint omits `before`. |
| `FSIM-SV-PARSE-280` | error | A solve-before constraint omits its terminating semicolon. |
| `FSIM-SV-PARSE-281` | error | A SystemVerilog real/time literal has a malformed mantissa or an unrepresentable source exponent. |
| `FSIM-SV-PARSE-282` | error | A decimal real literal is immediately followed by an unrecognized time-unit spelling. |
| `FSIM-SV-PARSE-283` | error | Expected `@` before a clocking-block event. |
| `FSIM-SV-PARSE-284` | error | Expected `;` after a clocking-block event. |
| `FSIM-SV-PARSE-285` | error | Expected `;` after a clocking signal declaration. |
| `FSIM-SV-PARSE-286` | error | Expected `endclocking` after a clocking block. |
| `FSIM-SV-PARSE-287` | error | Expected `;` after default clocking skews. |
| `FSIM-SV-PARSE-288` | error | Expected `;` after a default clocking declaration. |
| `FSIM-SV-PARSE-289` | error | Expected `;` after a virtual-interface declaration. |
| `FSIM-SV-PARSE-290` | error | Expected a sequence, property, or checker declaration/end-label name. |
| `FSIM-SV-PARSE-291` | error | Expected `;` after a sequence, property, or checker declaration header. |
| `FSIM-SV-PARSE-292` | error | Expected the matching `endsequence`, `endproperty`, or `endchecker` terminator. |
| `FSIM-SV-PARSE-293` | error | A sequence, property, or checker formal header is not one balanced parenthesized list. |
| `FSIM-SV-PARSE-294` | error | A sequence, property, or checker formal argument is empty or has no name. |
| `FSIM-SV-PARSE-295` | error | An assertion local-variable declaration or declarator has no name. |
| `FSIM-SV-PARSE-296` | error | An assertion declaration clock has no valid balanced event expression. |
| `FSIM-SV-PARSE-297` | error | An assertion `disable iff` clause has no valid balanced condition. |
| `FSIM-SV-PARSE-298` | error | A sequence concatenation has no valid delay value or balanced delay range. |
| `FSIM-SV-PARSE-299` | error | A sequence expression, concatenation element, or repetition operand is empty. |
| `FSIM-SV-PARSE-300` | error | A sequence `intersect` operator has an empty left or right operand. |
| `FSIM-SV-PARSE-301` | error | A sequence `throughout`/`within` operand or `first_match` argument is missing or malformed. |
| `FSIM-SV-PARSE-302` | error | A sequence `.matched` or `.triggered` endpoint receiver or method call is malformed. |
| `FSIM-SV-PARSE-303` | error | A property implication operand or property delay value/range is missing or malformed. |
| `FSIM-SV-PARSE-304` | error | A property `until` operand or `nexttime` count/operand is missing or malformed. |
| `FSIM-SV-PARSE-305` | error | A property recurrence range/operand or `strong`/`weak` sequence wrapper is missing or malformed. |
| `FSIM-SV-PARSE-306` | error | A property accept/reject abort condition or property operand is missing or malformed. |
| `FSIM-SV-PARSE-307` | error | A concurrent assert/assume/cover/restrict property boundary is missing, empty, or malformed. |
| `FSIM-SV-PARSE-308` | error | A concurrent assertion pass or failure action is missing or unbalanced. |
| `FSIM-SV-PARSE-309` | error | A covergroup declaration or closing label has no name. |
| `FSIM-SV-PARSE-310` | error | A covergroup declaration header has no terminating semicolon. |
| `FSIM-SV-PARSE-311` | error | A covergroup declaration has no matching `endgroup`. |
| `FSIM-SV-PARSE-312` | error | An `endgroup :` closing label has no covergroup name. |
| `FSIM-SV-PARSE-313` | error | A covergroup formal argument list has unbalanced delimiters. |
| `FSIM-SV-PARSE-314` | error | A covergroup constructor or sample formal argument is empty, unnamed, or has an empty default. |
| `FSIM-SV-PARSE-315` | error | A covergroup sampling event or `with function sample` profile is malformed. |
| `FSIM-SV-PARSE-316` | error | A covergroup-scope `option` or `type_option` assignment is malformed. |
| `FSIM-SV-PARSE-317` | error | A coverpoint/cross boundary, expression, or operand list is missing or malformed. |
| `FSIM-SV-PARSE-318` | error | A coverpoint or cross `iff` guard is empty, unbalanced, or malformed. |
| `FSIM-SV-PARSE-319` | error | An explicit coverpoint bin declaration is missing its name, selection, or nonempty value list. |
| `FSIM-SV-PARSE-320` | error | A coverpoint bin value, range, wildcard literal, selection, or array declarator is malformed or cannot be represented by the bounded scalar coverage model. |
| `FSIM-SV-PARSE-321` | error | A transition bin has an empty, unbalanced, malformed, or unbounded sequence, repetition, or delay form. |
| `FSIM-SV-PARSE-322` | error | A coverpoint bin `iff` guard is empty, unbalanced, malformed, or attached to an invalid default-sequence form. |
| `FSIM-SV-PARSE-323` | error | An explicit cross bin is missing its name, assignment, or nonempty selection expression. |
| `FSIM-SV-PARSE-324` | error | A SystemVerilog DPI import or export declaration is missing its terminating semicolon. |
| `FSIM-SV-PARSE-325` | error | A SystemVerilog DPI declaration is missing its `function` or `task` callable kind. |
| `FSIM-SV-PARSE-326` | error | A SystemVerilog DPI declaration is missing its SystemVerilog callable name. |
| `FSIM-SV-PARSE-327` | error | A DPI import has a missing or malformed formal list, or a DPI export contains profile tokens after its name. |
| `FSIM-SV-PARSE-328` | error | A DPI function return type or formal type/name entry is missing or malformed. |
| `FSIM-SV-PARSE-329` | error | A SystemVerilog void-cast statement is missing its terminating semicolon. |
| `FSIM-SV-PARSE-330` | error | A procedural SystemVerilog `foreach` statement is missing its opening parenthesis. |
| `FSIM-SV-PARSE-331` | error | A procedural SystemVerilog `foreach` collection is missing its opening index bracket. |
| `FSIM-SV-PARSE-332` | error | A procedural SystemVerilog `foreach` index is missing its closing bracket. |
| `FSIM-SV-PARSE-333` | error | A procedural SystemVerilog `foreach` header is missing its closing parenthesis. |
| `FSIM-SV-PARSE-334` | error | A `randomize with` clause is missing its opening constraint brace. |
| `FSIM-SV-PARSE-335` | error | A `randomize with` constraint block is unterminated. |
| `FSIM-SV-PARSE-336` | error | A Verilog attribute instance has an empty, missing, or malformed attribute specification. |
| `FSIM-SV-PARSE-337` | error | A Verilog attribute instance is missing its closing `*)`. |
| `FSIM-SV-PARSE-338` | error | A Verilog attribute assignment is missing its constant expression. |
| `FSIM-SV-PARSE-339` | error | A `defparam` hierarchy index is missing its closing bracket. |
| `FSIM-SV-PARSE-340` | error | A `defparam` target is missing its assignment operator. |
| `FSIM-SV-PARSE-341` | error | A `defparam` declaration is missing its terminating semicolon. |
| `FSIM-SV-PARSE-342` | error | A procedural continuous assignment is missing its assignment operator. |
| `FSIM-SV-PARSE-343` | error | A procedural continuous assignment is missing its terminating semicolon. |
| `FSIM-SV-PARSE-344` | error | A procedural `deassign` statement is missing its terminating semicolon. |
| `FSIM-SV-PARSE-345` | error | A concatenated assignment target is missing its closing brace. |
| `FSIM-SV-PARSE-346` | error | A declaration, type, parameter, lifetime, port, initializer, or net-type form requires a later selected Verilog/SystemVerilog revision. |
| `FSIM-SV-PARSE-347` | error | An expression, operator, cast/pattern, assignment, timing control, process, loop, task, or function form requires a later selected Verilog/SystemVerilog revision. |
| `FSIM-SV-PARSE-348` | error | A hierarchy, configuration, generate, bind, assertion/coverage, class/constraint, interface/modport, package/import, or compilation-unit form requires a later selected Verilog/SystemVerilog revision, or configuration syntax is disabled by `verilog-2001-noconfig`. |
| `FSIM-SV-PARSE-349` | error | A predefined scope, value, type, method, constraint operator, or assertion/property operator requires a later selected SystemVerilog revision. |
| `FSIM-SV-PARSE-350` | error | A system task/function or a later service signature requires a later selected Verilog/SystemVerilog revision. |
| `FSIM-SV-PARSE-351` | error | A DPI declaration requires a selected SystemVerilog revision. |
| `FSIM-SV-PARSE-353` | error | An `extern` declaration is not followed by `module`, `interface`, or `program`. |
| `FSIM-SV-PARSE-354` | error | An indexed bind target or configuration instance path has an unterminated index. |
| `FSIM-SV-PARSE-355` | error | A SystemVerilog configuration name is missing its terminating semicolon. |
| `FSIM-SV-PARSE-356` | error | A SystemVerilog configuration `design` statement is missing its terminating semicolon. |
| `FSIM-SV-PARSE-357` | error | A SystemVerilog configuration `default` clause is missing `liblist`. |
| `FSIM-SV-PARSE-358` | error | A SystemVerilog configuration default library list is missing its terminating semicolon. |
| `FSIM-SV-PARSE-359` | error | A SystemVerilog configuration contains a clause other than `design`, `default`, `instance`, `cell`, or `endconfig`. |
| `FSIM-SV-PARSE-360` | error | A nested configuration selection is missing `config` after its colon. |
| `FSIM-SV-PARSE-361` | error | A SystemVerilog configuration instance or cell rule has neither `use` nor `liblist`. |
| `FSIM-SV-PARSE-362` | error | A SystemVerilog configuration instance or cell rule is missing its terminating semicolon. |
| `FSIM-SV-PARSE-363` | error | A SystemVerilog configuration is missing `endconfig`. |
| `FSIM-SV-PARSE-364` | error | A `wait_order` statement is missing its opening parenthesis. |
| `FSIM-SV-PARSE-365` | error | A `wait_order` statement has no named event. |
| `FSIM-SV-PARSE-366` | error | A `wait_order` event list is missing its closing parenthesis. |
| `FSIM-SV-PARSE-367` | error | A `wait_order` success or failure action statement is missing. |
| `FSIM-SV-PARSE-044` | error | Expected an immediate-assertion pass or failure action statement. |
| `FSIM-SV-PARSE-045` | error | Expected `;` after a procedural variable declaration. |
| `FSIM-SV-PARSE-046` | error | Expected `(` after `case`. |
| `FSIM-SV-PARSE-047` | error | Expected `)` after a `case` selector expression. |
| `FSIM-SV-PARSE-048` | error | Expected `:` after a `case` item. |
| `FSIM-SV-PARSE-049` | error | Expected `endcase`. |
| `FSIM-SV-PARSE-050` | error | A value parameter has no default constant expression. |
| `FSIM-SV-PARSE-051` | error | Expected `;` after a body parameter declaration. |
| `FSIM-SV-PARSE-052` | error | Expected `(` after a module parameter `#`. |
| `FSIM-SV-PARSE-053` | error | A module parameter-port item does not begin with `parameter` or `localparam`. |
| `FSIM-SV-PARSE-054` | error | Expected `)` after a module parameter-port list. |
| `FSIM-SV-PARSE-055` | error | Expected `(` after an instance parameter `#`. |
| `FSIM-SV-PARSE-056` | error | Expected `(` after a named parameter override. |
| `FSIM-SV-PARSE-057` | error | Expected `)` after a named parameter override. |
| `FSIM-SV-PARSE-058` | error | Expected `)` after instance parameter overrides. |
| `FSIM-SV-PARSE-059` | error | Expected `(` after a generate `if`. |
| `FSIM-SV-PARSE-060` | error | Expected `)` after a generate condition. |
| `FSIM-SV-PARSE-063` | error | A generate branch lacks `end` or has a mismatched end label. |
| `FSIM-SV-PARSE-064` | error | Expected `endgenerate`. |
| `FSIM-SV-PARSE-065` | error | Expected `(` after a generate `for`. |
| `FSIM-SV-PARSE-066` | error | An executable generate-loop variable has neither an inline nor module-scope `genvar` declaration. |
| `FSIM-SV-PARSE-067` | error | Expected `=` after a generate-loop variable. |
| `FSIM-SV-PARSE-068` | error | Expected `;` after a generate-loop initializer. |
| `FSIM-SV-PARSE-069` | error | Expected `;` after a generate-loop condition. |
| `FSIM-SV-PARSE-070` | error | A generate-loop iteration assigns a name other than its loop variable. |
| `FSIM-SV-PARSE-071` | error | A generate-loop iteration is not an assignment, prefix/postfix increment/decrement, `+=`, or `-=` update. |
| `FSIM-SV-PARSE-072` | error | Expected `)` after a generate-loop header. |
| `FSIM-SV-PARSE-073` | error | Expected `(` after a generate `case`. |
| `FSIM-SV-PARSE-074` | error | Expected `)` after a generate-case selector. |
| `FSIM-SV-PARSE-075` | error | Expected `:` after generate-case choices. |
| `FSIM-SV-PARSE-076` | error | Expected `endcase` for a generate case. |
| `FSIM-SV-PARSE-077` | error | Expected `;` after a module-scope `genvar` declaration. |
| `FSIM-SV-PARSE-078` | error | Expected `::` after a package name in an import clause. |
| `FSIM-SV-PARSE-079` | error | Expected `;` after a package import. |
| `FSIM-SV-PARSE-080` | error | Expected `;` after a SystemVerilog package header. |
| `FSIM-SV-PARSE-081` | error | Expected `endpackage` for a package declaration. |
| `FSIM-SV-PARSE-082` | error | Expected `;` after a bounded SystemVerilog typedef declaration. |
| `FSIM-SV-PARSE-083` | error | Expected `{` before the literals of a bounded enum typedef. |
| `FSIM-SV-PARSE-084` | error | Expected `}` after the literals of a bounded enum typedef. |
| `FSIM-SV-PARSE-085` | error | A bounded enum typedef has no literals. |
| `FSIM-SV-PARSE-086` | error | Expected `{` before bounded packed-aggregate members. |
| `FSIM-SV-PARSE-087` | error | Expected `}` after bounded packed-aggregate members. |
| `FSIM-SV-PARSE-088` | error | Expected `;` after a packed-aggregate member declaration. |
| `FSIM-SV-PARSE-089` | error | A bounded packed aggregate has no members. |
| `FSIM-SV-PARSE-090` | error | A replication concatenation has no repeated operands. |
| `FSIM-SV-PARSE-091` | error | Expected `(` after a built-in gate primitive and its optional instance name. |
| `FSIM-SV-PARSE-092` | error | A built-in gate primitive does not begin its terminal list with an output lvalue. |
| `FSIM-SV-PARSE-093` | error | Expected `)` after a built-in gate primitive terminal list. |
| `FSIM-SV-PARSE-094` | error | Expected `;` after a built-in gate primitive instance. |
| `FSIM-SV-PARSE-095` | error | Expected `(` after a procedural `for`. |
| `FSIM-SV-PARSE-096` | error | Expected `=` after an inline procedural loop variable. |
| `FSIM-SV-PARSE-097` | error | Expected `;` after a procedural loop initializer. |
| `FSIM-SV-PARSE-098` | error | Expected `;` after a procedural loop condition. |
| `FSIM-SV-PARSE-099` | error | Expected `)` after a procedural loop header. |
| `FSIM-SV-PARSE-100` | error | Expected `(` after a procedural `repeat`. |
| `FSIM-SV-PARSE-101` | error | Expected `)` after a procedural repeat count. |
| `FSIM-SV-PARSE-102` | error | Expected `(` after a procedural `while`. |
| `FSIM-SV-PARSE-103` | error | Expected `)` after a procedural while condition. |
| `FSIM-SV-PARSE-104` | error | Expected `;` after a SystemVerilog `break` or `continue` statement. |
| `FSIM-SV-PARSE-105` | error | Expected `while` after a SystemVerilog `do` body. |
| `FSIM-SV-PARSE-106` | error | Expected `(` before a SystemVerilog do-while condition. |
| `FSIM-SV-PARSE-107` | error | Expected `)` after a SystemVerilog do-while condition. |
| `FSIM-SV-PARSE-108` | error | Expected `;` after a SystemVerilog do-while statement. |
| `FSIM-SV-PARSE-109` | error | Expected `(` after a Verilog/SystemVerilog `wait`. |
| `FSIM-SV-PARSE-110` | error | Expected `)` after a Verilog/SystemVerilog wait condition. |
| `FSIM-SV-PARSE-111` | error | Expected `final` at the start of a SystemVerilog final procedure. |
| `FSIM-SV-PARSE-112` | error | Expected `)` after a Verilog/SystemVerilog `$stop` argument. |
| `FSIM-SV-PARSE-113` | error | Expected `;` after a Verilog/SystemVerilog `$stop` task. |
| `FSIM-SV-PARSE-115` | error | Expected `)` after SystemVerilog `$fatal` arguments. |
| `FSIM-SV-PARSE-116` | error | Expected `;` after a SystemVerilog `$fatal` task. |
| `FSIM-SV-PARSE-117` | error | Expected `;` after a named-event declaration. |
| `FSIM-SV-PARSE-118` | error | Expected `;` after an immediate named-event trigger. |
| `FSIM-SV-PARSE-119` | error | Expected `)` after Verilog/SystemVerilog `$display` arguments. |
| `FSIM-SV-PARSE-120` | error | Expected `;` after a Verilog/SystemVerilog `$display` task. |
| `FSIM-SV-PARSE-121` | error | Expected `)` after Verilog/SystemVerilog `$write` arguments. |
| `FSIM-SV-PARSE-122` | error | Expected `;` after a Verilog/SystemVerilog `$write` task. |
| `FSIM-SV-PARSE-123` | error | Expected `)` after Verilog/SystemVerilog `$strobe` arguments. |
| `FSIM-SV-PARSE-124` | error | Expected `;` after a Verilog/SystemVerilog `$strobe` task. |
| `FSIM-SV-PARSE-125` | error | Expected `)` after Verilog/SystemVerilog `$monitor` arguments. |
| `FSIM-SV-PARSE-126` | error | Expected `;` after a Verilog/SystemVerilog `$monitor` task. |
| `FSIM-SV-PARSE-127` | error | Expected `)` after a Verilog/SystemVerilog `$monitoron` or `$monitoroff` task. |
| `FSIM-SV-PARSE-128` | error | Expected `;` after a Verilog/SystemVerilog `$monitoron` or `$monitoroff` task. |
| `FSIM-SV-PARSE-129` | error | Expected `)` after SystemVerilog `$info`, `$warning`, or `$error` arguments. |
| `FSIM-SV-PARSE-130` | error | Expected `;` after a SystemVerilog `$info`, `$warning`, or `$error` task. |
| `FSIM-SV-PARSE-131` | error | Expected a numeric magnitude in a SystemVerilog time declaration. |
| `FSIM-SV-PARSE-132` | error | Expected a physical unit in a SystemVerilog time declaration. |
| `FSIM-SV-PARSE-133` | error | Expected `;` after a SystemVerilog time declaration. |
| `FSIM-SV-PARSE-134` | error | Expected the second `:` and maximum value in a `min:typ:max` delay triple. |
| `FSIM-SV-PARSE-135` | error | Expected another delay value after a comma in a parenthesized transition-delay list. |
| `FSIM-SV-PARSE-136` | error | An event control has an empty event-expression list. |
| `FSIM-SV-PARSE-039` | error | Expected a time-unit magnitude after `` `timescale``. |
| `FSIM-SV-PARSE-040` | error | Expected a time-unit name after the `` `timescale`` magnitude. |
| `FSIM-SV-PARSE-041` | error | Expected `/` between `` `timescale`` unit and precision. |
| `FSIM-SV-PARSE-042` | error | Expected a time-precision magnitude after `/`. |
| `FSIM-SV-PARSE-043` | error | Expected a time-precision unit. |

### Verilog/SystemVerilog semantics and bounded-subset rejections

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-VERILOG-SEM-001` | error | `always_ff` was used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-VERILOG-SEM-002` | error | `always_comb` was used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-VERILOG-SEM-003` | error | `always_latch` was used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-VERILOG-SEM-004` | error | A SystemVerilog-only wildcard equality operator was used in Verilog-2005. |
| `FSIM-VERILOG-SEM-005` | error | Compound assignments or standalone increment/decrement were used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-VERILOG-SEM-006` | error | A `final` procedure was used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-VERILOG-SEM-007` | error | `$fatal` was used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-VERILOG-SEM-008` | error | A nonblocking named-event trigger (`->>`) was used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-VERILOG-SEM-009` | error | `$info`, `$warning`, or `$error` was used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-VERILOG-SEM-010` | error | An increment or decrement expression was used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-VERILOG-SEM-012` | error | A Verilog-2005 memory declaration has more than one unpacked dimension. |
| `FSIM-VERILOG-SEM-013` | error | A declaration appears in a Verilog-2005 procedural `for` initializer rather than in the surrounding named block. |
| `FSIM-VERILOG-SEM-014` | error | A named-event declaration assignment was used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-SV-SEM-002` | error | A delay magnitude is not a decimal literal. |
| `FSIM-SV-SEM-003` | error | Duplicate module-port declaration in the module header. |
| `FSIM-SV-SEM-004` | error | Duplicate non-ANSI body port declaration. |
| `FSIM-SV-SEM-005` | error | Duplicate declaration or type refinement of a non-ANSI port. |
| `FSIM-SV-SEM-006` | error | Duplicate internal signal declaration. |
| `FSIM-SV-SEM-007` | error | Invalid `` `timescale`` magnitude or time unit. |
| `FSIM-SV-SEM-008` | error | A `` `timescale`` value exceeds fsim's 64-bit time range. |
| `FSIM-SV-SEM-009` | error | `` `timescale`` precision is coarser than its time unit. |
| `FSIM-SV-SEM-010` | error | A delay overflows after applying the module `` `timescale``. |
| `FSIM-SV-SEM-011` | error | An `always_comb` or `always_latch` has an explicit event control. |
| `FSIM-SV-SEM-012` | error | An `always_comb` or `always_latch` contains a timing control. |
| `FSIM-SV-SEM-013` | error | An `always_comb` or `always_latch` contains a nonblocking assignment. |
| `FSIM-SV-SEM-014` | error | A `case` statement contains more than one `default` item. |
| `FSIM-SV-SEM-015` | error | An undeclared implicit net is forbidden by `` `default_nettype none``. |
| `FSIM-SV-SEM-016` | error | An untyped ANSI or non-ANSI port is forbidden by `` `default_nettype none``. |
| `FSIM-SV-SEM-017` | error | A parameter name is declared more than once in its module or generate scope. |
| `FSIM-SV-SEM-018` | error | A named parameter override is repeated on one instance. |
| `FSIM-SV-SEM-019` | error | Named and positional parameter overrides are mixed on one instance. |
| `FSIM-SV-SEM-020` | error | A parameter conflicts with a port or signal declaration in the same module or generate namespace. |
| `FSIM-SV-SEM-021` | error | A generate case contains more than one `default` item. |
| `FSIM-SV-SEM-022` | error | A module-scope `genvar` declaration is duplicated or conflicts with another object. |
| `FSIM-SV-SEM-023` | error | A package declaration end name does not match its opening name. |
| `FSIM-SV-SEM-024` | error | A bounded scope declares the same typedef name more than once. |
| `FSIM-SV-SEM-025` | error | A bounded packed aggregate declares the same member name more than once. |
| `FSIM-SV-SEM-026` | error | A bounded built-in gate primitive has an invalid number of input terminals. |
| `FSIM-SV-SEM-103` | error | A procedural loop update is not an assignment or increment of its loop variable. |
| `FSIM-SV-SEM-106` | error | A body-timed `always` process has a reachable re-entry path without suspension or termination. |
| `FSIM-SV-SEM-107` | error | `wait fork` or `disable fork` is used outside SystemVerilog-2017 input. |
| `FSIM-SV-SEM-108` | error | `join_any` or `join_none` is used outside SystemVerilog-2017 input. |
| `FSIM-SV-SEM-109` | error | A procedural fork closing label has no opening label or does not match it. |
| `FSIM-SV-SEM-110` | error | A net-declaration delay is attached to a variable rather than a `wire` net. |
| `FSIM-SV-SEM-111` | error | A static gate-instance array has no instance name. |
| `FSIM-SV-SEM-112` | error | A static gate-instance array bound is not a decimal locally static integer in the bounded slice. |
| `FSIM-SV-SEM-113` | error | Materializing a static gate-instance array would exceed the frontend owning-storage budget. |
| `FSIM-SV-SEM-114` | error | A gate-array terminal is neither scalar nor equal in width to the instance count. |
| `FSIM-SV-SEM-115` | error | A modport declaration appears outside a SystemVerilog interface. |
| `FSIM-SV-SEM-116` | error | A modport signal member has no explicit direction. |
| `FSIM-SV-SEM-117` | error | A modport repeats a member name. |
| `FSIM-SV-SEM-118` | error | A modport names a signal that is not declared by its interface. |
| `FSIM-SV-SEM-119` | error | An interface repeats a modport declaration name. |
| `FSIM-SV-SEM-120` | error | A bounded instance-array range is not a decimal locally static integer range. |
| `FSIM-SV-SEM-121` | error | Materializing a static instance array would exceed the frontend owning-storage budget. |
| `FSIM-SV-SEM-122` | error | A modport import/export entry does not name an interface function or task. |
| `FSIM-SV-SEM-123` | error | A modport callable's explicit function/task kind does not match its interface declaration. |
| `FSIM-SV-SEM-124` | error | A generated typedef or enum literal conflicts with another declaration in the same generated body. |
| `FSIM-SV-SEM-125` | error | A type cast appears outside SystemVerilog-2017 input. |
| `FSIM-SV-SEM-127` | error | A bounded SystemVerilog string method has the wrong number of arguments. |
| `FSIM-SV-SEM-128` | error | A dynamic-array `new[size](initializer)` expression has other than one initializer. |
| `FSIM-SV-SEM-129` | error | A module, interface, or program declaration end name does not match its opening name. |
| `FSIM-SV-SEM-130` | error | A user-defined primitive declares more than one output terminal. |
| `FSIM-SV-SEM-131` | error | A sequential user-defined primitive `reg` refinement does not name its output. |
| `FSIM-SV-SEM-132` | error | A user-defined primitive initial statement does not assign its output. |
| `FSIM-SV-SEM-133` | error | A user-defined primitive initial value is not scalar `0`, `1`, or `x`. |
| `FSIM-SV-SEM-134` | error | A user-defined primitive closing name does not match its opening name. |
| `FSIM-SV-SEM-135` | error | A Verilog compilation unit repeats a user-defined primitive design name. |
| `FSIM-SV-SEM-136` | error | User-defined primitive terminal declarations do not name one output first followed by every header input in order. |
| `FSIM-SV-SEM-137` | error | A user-defined primitive header repeats a terminal name. |
| `FSIM-SV-SEM-138` | error | A user-defined primitive has no input terminal. |
| `FSIM-SV-SEM-139` | error | A sequential user-defined primitive output is not declared `reg`. |
| `FSIM-SV-SEM-140` | error | A user-defined primitive table has no rows. |
| `FSIM-SV-SEM-141` | error | A user-defined primitive table row has the wrong number of input symbols. |
| `FSIM-SV-SEM-142` | error | A combinational user-defined primitive table row contains an edge symbol. |
| `FSIM-SV-SEM-143` | error | A sequential user-defined primitive table row contains more than one edge symbol. |
| `FSIM-SV-SEM-144` | error | A user-defined primitive table row duplicates an earlier input/state pattern. |
| `FSIM-SV-SEM-145` | error | A user-defined primitive propagation delay is not a representable nonnegative decimal expression. |
| `FSIM-SV-SEM-146` | error | A user-defined primitive propagation delay supplies more than three transition values. |
| `FSIM-SV-SEM-147` | error | Materializing a user-defined primitive table would exceed the frontend owning-storage budget. |
| `FSIM-SV-SEM-148` | error | A drive-strength pair does not contain one zero strength and one one strength. |
| `FSIM-SV-SEM-149` | error | A net declaration contains an invalid charge-strength spelling. |
| `FSIM-SV-SEM-150` | error | A variable declaration carries net drive-strength syntax. |
| `FSIM-SV-SEM-151` | error | A charge strength is attached to a declaration other than `trireg`. |
| `FSIM-SV-SEM-152` | error | A MOS or transmission primitive carries illegal drive-strength syntax. |
| `FSIM-SV-SEM-153` | error | A switch primitive has the wrong terminal count. |
| `FSIM-SV-SEM-154` | error | A single pull strength has the wrong output polarity. |
| `FSIM-SV-SEM-155` | error | Both members of a drive-strength pair specify high impedance. |
| `FSIM-SV-SEM-156` | error | A switch-instance array range is not locally static. |
| `FSIM-SV-SEM-157` | error | A switch-instance array exceeds the frontend owning-storage budget. |
| `FSIM-SV-SEM-158` | error | A switch-array terminal is neither scalar nor width-matched to the array. |
| `FSIM-SV-SEM-164` | error | A specify block is used outside a module or a module path has an unsupported transition-delay arity. |
| `FSIM-SV-SEM-165` | error | A module repeats a `specparam` declaration name across its specify blocks. |
| `FSIM-SV-SEM-166` | error | A `$width` timing check omits its threshold while supplying a later optional argument. |
| `FSIM-SV-SEM-167` | error | A scope contains more than one defining declaration of the same SystemVerilog class. |
| `FSIM-SV-SEM-168` | error | The identifier following `endclass` does not match the class name. |
| `FSIM-SV-SEM-169` | error | A class property is qualified with both `rand` and `randc`. |
| `FSIM-SV-SEM-170` | error | A class repeats a property declaration name. |
| `FSIM-SV-SEM-171` | error | A pure class method is not also declared virtual. |
| `FSIM-SV-SEM-172` | error | A compilation-unit class method definition lacks a class-qualified name. |
| `FSIM-SV-SEM-173` | error | A class repeats a constraint declaration name. |
| `FSIM-SV-SEM-174` | error | A `chandle` assignment or cast has a numeric, aggregate, or otherwise incompatible source or destination. |
| `FSIM-SV-SEM-175` | error | A `chandle` is used for arithmetic or logical truth, or compared with a non-`chandle` value. |
| `FSIM-SV-SEM-176` | error | A clocking signal declaration omits its input, output, or inout direction. |
| `FSIM-SV-SEM-177` | error | A clocking block repeats a clocking signal name. |
| `FSIM-SV-SEM-178` | error | An unaliased clocking signal is not declared by its design unit. |
| `FSIM-SV-SEM-179` | error | The identifier following `endclocking` does not match the clocking block name. |
| `FSIM-SV-SEM-180` | error | A design unit repeats a clocking block name. |
| `FSIM-SV-SEM-181` | error | A default clocking declaration omits its direction or skew. |
| `FSIM-SV-SEM-182` | error | A clocking block repeats its default input or output skew. |
| `FSIM-SV-SEM-183` | error | An inout clocking signal declares an illegal skew. |
| `FSIM-SV-SEM-184` | error | A procedural `##` cycle delay is used outside SystemVerilog-2017. |
| `FSIM-SV-SEM-185` | error | A design unit declares more than one default clocking block. |
| `FSIM-SV-SEM-186` | error | A default clocking declaration selects an undeclared block. |
| `FSIM-SV-SEM-187` | error | A design-unit `default` declaration does not select a clocking block. |
| `FSIM-SV-SEM-188` | error | A virtual interface has no interface type name. |
| `FSIM-SV-SEM-189` | error | A virtual-interface variable conflicts with another design-unit object. |
| `FSIM-SV-SEM-190` | error | A modport clocking member does not name a clocking block declared by its interface. |
| `FSIM-SV-SEM-191` | error | A sequence, property, or checker declaration appears outside SystemVerilog-2017. |
| `FSIM-SV-SEM-192` | error | A design unit repeats a sequence, property, or checker declaration name. |
| `FSIM-SV-SEM-193` | error | A sequence, property, or checker closing name does not match its declaration. |
| `FSIM-SV-SEM-194` | error | A sequence, property, or checker repeats a formal-argument name. |
| `FSIM-SV-SEM-195` | error | An assertion local variable conflicts with a formal or earlier local variable. |
| `FSIM-SV-SEM-196` | error | An unqualified assertion reference is not declared in assertion or design-unit scope. |
| `FSIM-SV-SEM-197` | error | An unqualified sequence endpoint receiver does not name a sequence declaration or sequence formal. |
| `FSIM-SV-SEM-198` | error | A restrict property action or cover property failure action is not permitted. |
| `FSIM-SV-SEM-199` | error | An executable property or sequence has an invalid actual/formal profile, duplicate attempt-local name, unsupported local type or initializer, or a match assignment that does not target an attempt local. |
| `FSIM-SV-SEM-200` | error | An executable concurrent-property predicate is outside the supported packed scalar/vector expression subset. |
| `FSIM-SV-SEM-201` | error | An executable concurrent-property clock is not one direct design-unit object with an optional edge. |
| `FSIM-SV-SEM-203` | error | A covergroup declaration is used outside SystemVerilog-2017. |
| `FSIM-SV-SEM-204` | error | A covergroup closing name does not match its declaration name. |
| `FSIM-SV-SEM-205` | error | A covergroup name duplicates an earlier declaration in the same owner. |
| `FSIM-SV-SEM-206` | error | A covergroup constructor or sample formal name is duplicated in its profile. |
| `FSIM-SV-SEM-207` | error | A coverpoint or cross name duplicates an earlier declaration in the same covergroup. |
| `FSIM-SV-SEM-208` | error | An unqualified coverage expression name is not visible from its covergroup scope. |
| `FSIM-SV-SEM-209` | error | A cross operand does not resolve to an explicit coverpoint or visible implicit-coverpoint expression. |
| `FSIM-SV-SEM-210` | error | Covergroup constructor or sample actuals do not match the resolved formal profile. |
| `FSIM-SV-SEM-211` | error | A covergroup type name resolves ambiguously in its lexical/package/class scope. |
| `FSIM-SV-SEM-212` | error | A coverpoint declares the same explicit bin name more than once. |
| `FSIM-SV-SEM-213` | error | A coverpoint bin array size or expansion is empty, exceeds the bounded 65,536-bin/value limit, or would create an empty expanded bin. |
| `FSIM-SV-SEM-214` | error | A transition-bin array expansion exceeds 65,536 sequences or would create an empty expanded bin. |
| `FSIM-SV-SEM-215` | error | A cross declares the same explicit cross-bin name more than once. |
| `FSIM-SV-SEM-216` | error | A `binsof` operand or named-bin qualification is empty, unknown, malformed, or ambiguous in its cross. |
| `FSIM-SV-SEM-222` | error | A DPI declaration uses a link string other than `"DPI-C"`. |
| `FSIM-SV-SEM-223` | error | A DPI export incorrectly declares a `pure` or `context` import qualifier. |
| `FSIM-SV-SEM-224` | error | An imported DPI task is incorrectly declared `pure`. |
| `FSIM-SV-SEM-225` | error | A pure imported DPI function has a non-input formal. |
| `FSIM-SV-SEM-226` | error | A DPI C linkage alias is not a portable C identifier. |
| `FSIM-SV-SEM-227` | error | A DPI formal incorrectly declares a default value. |
| `FSIM-SV-SEM-228` | error | A compilation-unit callable or owner-local DPI SystemVerilog/C export name is duplicated. |
| `FSIM-SV-SEM-229` | error | A DPI import conflicts with a native callable in the same owner scope. |
| `FSIM-SV-SEM-230` | error | A DPI export does not resolve to a native callable of the declared kind in the same owner scope. |
| `FSIM-SV-SEM-231` | error | A `defparam` target does not name both an instance hierarchy and a parameter. |
| `FSIM-SV-SEM-232` | error | The final parameter segment of a `defparam` target is incorrectly indexed. |
| `FSIM-SV-SEM-233` | error | A SystemVerilog configuration contains more than one `design` statement. |
| `FSIM-SV-SEM-234` | error | A SystemVerilog configuration contains more than one default library list. |
| `FSIM-SV-SEM-235` | error | A SystemVerilog configuration end name does not match its declaration name. |
| `FSIM-SV-SEM-236` | error | A SystemVerilog configuration has no required `design` statement. |
| `FSIM-SV-SEM-237` | error | A SystemVerilog nettype declaration duplicates or conflicts with another visible type declaration. |
| `FSIM-SV-SEM-238` | error | A SystemVerilog alias statement has fewer than two lvalue terminals. |
| `FSIM-SV-SEM-239` | error | A SystemVerilog let declaration duplicates or conflicts with another visible callable declaration. |
| `FSIM-SV-SEM-240` | error | A SystemVerilog let declaration repeats a formal name. |
| `FSIM-SV-SEM-241` | error | The SystemVerilog `type` operator does not have exactly one operand. |
| `FSIM-SV-SEM-242` | error | An ordinary packed union declares members with different widths. |
| `FSIM-SV-SEM-243` | error | A `wait_order` operand is not a named-event identifier. |
| `FSIM-SV-SEM-244` | error | An executable explicit/event covergroup sample uses a compound event or coverpoint expression outside the direct-owner-object execution slice. |
| `FSIM-SV-SEM-217` | error | A coverpoint or cross `weight`, `goal`, or `at_least` option is not a bounded integer in its permitted range. |
| `FSIM-SV-SEM-218` | error | A covergroup-level weight, goal, `per_instance`, or `merge_instances` literal is outside its permitted bounded range. |
| `FSIM-SV-SEM-219` | error | A covergroup constructor or sample formal uses a real, string, chandle, event, void, or other type outside the bounded integral coverage model. |
| `FSIM-SV-SEM-221` | error | A covergroup exceeds the bounded declaration, bin, transition-work, or cross-product resource budget. |
| `FSIM-SV-COV-001` | error | A sampled coverpoint value matched an `illegal_bins` declaration; the diagnostic reports the stable bin identity and value. |
| `FSIM-SV-COV-002` | error | A coverpoint or cross bin hit count reached the exact unsigned 64-bit resource bound and cannot be incremented. |
| `FSIM-SV-COV-003` | error | A coverage callback attempted to sample the same covergroup while its current sample transaction was active. |
| `FSIM-SV-COV-004` | error | A requested explicit, event-driven, or procedural sample trigger does not match the covergroup declaration profile. |
| `FSIM-SV-COV-005` | error | A coverage transaction exceeds its bounded input/work or persistent state-record storage budget before mutation. |
| `FSIM-SV-CLASS-001` | error | A class forward declaration has no defining declaration. |
| `FSIM-SV-CLASS-002` | error | A named base class is not visible from the declaring class scope. |
| `FSIM-SV-CLASS-003` | error | A named base class is ambiguous in lexical or import scope. |
| `FSIM-SV-CLASS-004` | error | A class-handle type name is ambiguous in lexical or import scope. |
| `FSIM-SV-CLASS-005` | error | More than one declaration resolves to the same canonical class identity. |
| `FSIM-SV-CLASS-006` | error | An out-of-block class method owner is missing or ambiguous. |
| `FSIM-SV-CLASS-007` | error | An out-of-block class method definition has no matching extern prototype. |
| `FSIM-SV-CLASS-008` | error | A class method has more than one out-of-block definition. |
| `FSIM-SV-CLASS-009` | error | An implemented interface-class name is not visible from the declaring class scope. |
| `FSIM-SV-CLASS-010` | error | An implemented interface-class name is ambiguous in lexical or import scope. |
| `FSIM-SV-CLASS-012` | error | A selected instance or static class property is not visible on the resolved receiver type. |
| `FSIM-SV-CLASS-013` | error | A class method selection has no compatible profile or remains ambiguous after argument association. |
| `FSIM-SV-CLASS-014` | error | A class construction expression has no destination class-handle type. |
| `FSIM-SV-CLASS-015` | error | `$cast` lacks a writable class-handle destination or compatible source expression. |
| `FSIM-SV-CLASS-019` | error | An object `randomize` variable list selects a missing or nonrandom property. |
| `FSIM-SV-CLASS-020` | error | A randomization mode call has invalid arity or selects no random property or constraint block. |
| `FSIM-SV-CLASS-021` | error | A randomization mode call violates local or protected class-member access. |
| `FSIM-SV-CLASS-INHERIT-001` | error | The class inheritance graph contains a cycle. |
| `FSIM-SV-CLASS-INHERIT-002` | error | A class contains duplicate method profiles. |
| `FSIM-SV-CLASS-INHERIT-003` | error | A concrete class contains a pure method declaration. |
| `FSIM-SV-CLASS-INHERIT-004` | error | A final class method is not virtual. |
| `FSIM-SV-CLASS-INHERIT-005` | error | An `implements` selection does not name an interface class. |
| `FSIM-SV-CLASS-INHERIT-006` | error | An overriding method has an incompatible result type, including a noncovariant class-handle result. |
| `FSIM-SV-CLASS-INHERIT-007` | error | A class overrides an inherited final method. |
| `FSIM-SV-CLASS-INHERIT-009` | error | A concrete class leaves an inherited pure method unimplemented. |
| `FSIM-SV-CLASS-SPEC-001` | error | A class parameter actual names no formal or exceeds the positional formal count. |
| `FSIM-SV-CLASS-SPEC-002` | error | A class parameter formal receives more than one actual. |
| `FSIM-SV-CLASS-SPEC-003` | error | A class type parameter receives a value rather than a data-type actual. |
| `FSIM-SV-CLASS-SPEC-004` | error | A class value-parameter actual is not locally constant. |
| `FSIM-SV-CLASS-SPEC-005` | error | Named and positional class parameter actuals are mixed. |
| `FSIM-SV-CLASS-SPEC-006` | error | A class type parameter has neither an actual nor a default. |
| `FSIM-SV-CLASS-SPEC-007` | error | A class value-parameter default is not locally constant. |
| `FSIM-SV-CLASS-SPEC-008` | error | Class specialization recursively requires itself. |
| `FSIM-SV-CLASS-SPEC-009` | error | A specialized class property has no finite materializable layout. |
| `FSIM-SV-CLASS-SPEC-010` | error | A class instance layout exceeds host-addressable storage. |
| `FSIM-SV-CLASS-SPEC-011` | error | The stable virtual-method slot domain is exhausted. |
| `FSIM-ELAB-SVCLASS-001` | error | `null` has no executable contextual class-handle type. |
| `FSIM-ELAB-SVCLASS-002` | error | A class allocation result is incompatible with its destination handle type. |
| `FSIM-ELAB-SVCLASS-003` | error | A constructor actual has no executable packed width. |
| `FSIM-ELAB-SVCLASS-004` | error | An instance class-property assignment is not a supported time-free blocking packed assignment. |
| `FSIM-ELAB-SVCLASS-005` | error | An instance class-property read lacks one receiver or an executable packed type. |
| `FSIM-ELAB-SVCLASS-006` | error | A class method call has inconsistent receiver, argument, or result metadata. |
| `FSIM-ELAB-SVCLASS-007` | error | A class method actual has no executable packed width. |
| `FSIM-ELAB-SVCLASS-008` | error | A static class-property assignment is not a supported time-free blocking packed assignment. |
| `FSIM-ELAB-SVCLASS-009` | error | A static class-property read has no executable packed type. |
| `FSIM-ELAB-SVCLASS-010` | error | A static class-method call has inconsistent argument or result metadata. |
| `FSIM-ELAB-SVCLASS-011` | error | A static class-method actual has no executable packed width. |
| `FSIM-ELAB-SVCLASS-012` | error | A class-handle container element assignment is not a supported blocking indexed assignment. |
| `FSIM-ELAB-SVCLASS-013` | error | A dynamic class-handle container assignment is not a supported `new[size]` operation. |
| `FSIM-ELAB-SVCONCAT-001` | error | A concatenated assignment or force target is empty, unsized, overflowing, or used with an unsupported update form. |
| `FSIM-ELAB-SVCLASS-014` | error | A class-handle container read lacks a receiver or index. |
| `FSIM-ELAB-SVCLASS-015` | error | A class-handle queue operation has an invalid argument profile. |
| `FSIM-ELAB-SVCLASS-016` | error | A class-handle container expression is not a supported `pop_front` or `size` call. |
| `FSIM-ELAB-SVCLASS-017` | error | `$cast` lacks a resolved destination class handle or source during lowering. |
| `FSIM-ELAB-SVCLASS-018` | error | Retired catalog identity: class-method string output and inout actuals now use the common executable copy-out path. |
| `FSIM-ELAB-SVCLASS-019` | error | Retired catalog identity: static class-method string output and inout actuals now use the common executable copy-out path. |
| `FSIM-ELAB-CLOCK-001` | error | A clocking block does not have exactly one signal event. |
| `FSIM-ELAB-CLOCK-002` | error | A clocking block event signal cannot be resolved. |
| `FSIM-ELAB-CLOCK-003` | error | A clocking member alias is not a signal identifier expression. |
| `FSIM-ELAB-CLOCK-004` | error | A clocking member signal or its executable packed type cannot be resolved. |
| `FSIM-ELAB-CLOCK-005` | error | A procedural `##` cycle delay has no default clocking block. |
| `FSIM-ELAB-CLOCK-006` | error | A `#1step` input skew has no concrete design-unit time precision. |
| `FSIM-ELAB-CLOCK-007` | error | A modport clocking member is absent from its retained interface HIR. |
| `FSIM-ELAB-SVRAND-001` | error | `std::randomize` has no arguments or appears outside SystemVerilog execution. |
| `FSIM-ELAB-SVRAND-002` | error | A `std::randomize` argument is not a writable local identifier. |
| `FSIM-ELAB-SVRAND-003` | error | A `std::randomize` argument is not a supported packed scalar local. |
| `FSIM-ELAB-SVRAND-004` | error | A packed `std::randomize` local has no nonempty width representable by SimIR metadata. |
| `FSIM-ELAB-SVRAND-005` | error | A parsed `randomize with` call has lost its retained structured constraint block. |
| `FSIM-ELAB-SVRAND-006` | error | An inline constraint uses a variable, constant, or operator that cannot be represented by the portable constraint graph. |
| `FSIM-ELAB-SVRAND-007` | error | `$srandom` is outside SystemVerilog, has the wrong arity, or has a nonintegral seed. |
| `FSIM-ELAB-SVRAND-008` | error | An IEEE random-distribution function has the wrong arity, a nonintegral argument, or a seed that is not a writable packed integer variable at least 32 bits wide. |
| `FSIM-ELAB-SVIFACE-006` | error | A process writes through a read-only input port or modport input member. |
| `FSIM-ELAB-SVIFACE-007` | error | A retained interface callable cannot be materialized at its same-language module boundary. |
| `FSIM-ELAB-SVIFACE-008` | error | An interface callable is visible more than once through the same module port. |
| `FSIM-ELAB-SVIFACE-009` | error | A modport export has no matching callable implementation in the connected module. |
| `FSIM-ELAB-SVIFACE-010` | error | A parameterized virtual-interface view does not match the connected interface specialization identity. |
| `FSIM-ELAB-SVIFACE-011` | error | A restricted modport actual is widened to a generic interface port or rebound to a different modport. |
| `FSIM-SV-SEM-031` | error | A SystemVerilog `break` or `continue` statement appears outside a procedural loop. |
| `FSIM-SV-SEM-032` | error | A SystemVerilog `final` procedure contains a timing control, wait, or `$finish`. |
| `FSIM-SV-SEM-033` | error | A SystemVerilog `final` procedure contains a nonblocking assignment. |
| `FSIM-SV-SEM-034` | error | A procedural block closing label has no opening label or does not match it. |
| `FSIM-SV-SEM-035` | error | A named event conflicts with another event, signal, or port declaration. |
| `FSIM-SV-SEM-036` | error | A delayed named-event trigger uses immediate `->` rather than nonblocking `->>` syntax. |
| `FSIM-SV-SEM-037` | error | A `$display` format string has more value-consuming conversions than value arguments. |
| `FSIM-SV-SEM-038` | error | A `$write` format string has more value-consuming conversions than value arguments. |
| `FSIM-SV-SEM-039` | error | A `$strobe`, `$fstrobe`, or `$fmonitor` format string has more value-consuming conversions than value arguments. |
| `FSIM-SV-SEM-040` | error | A Verilog/SystemVerilog output string uses an unsupported, incomplete, or out-of-byte-range escape. |
| `FSIM-SV-SEM-041` | error | A `$monitor` format string has more value-consuming conversions than value arguments. |
| `FSIM-SV-SEM-042` | error | An output format string uses an unsupported conversion/modifier, an invalid or overflowing field width, or a malformed percent escape. |
| `FSIM-SV-SEM-043` | error | A bounded `$info`, `$warning`, or `$error` call has a nonliteral or additional message argument. |
| `FSIM-SV-SEM-044` | error | A `timeunit` or `timeprecision` declaration appears in Verilog-2005 input. |
| `FSIM-SV-SEM-045` | error | A time declaration has an illegal magnitude or physical unit. |
| `FSIM-SV-SEM-046` | error | A scope repeats a `timeunit` or `timeprecision` declaration. |
| `FSIM-SV-SEM-047` | error | A compilation-unit or module time declaration appears after another item. |
| `FSIM-SV-SEM-048` | error | An effective SystemVerilog timeprecision is coarser than its timeunit or overflows. |
| `FSIM-SV-SEM-049` | error | A decimal delay literal cannot be represented by the exact bounded rational HIR form. |
| `FSIM-SV-SEM-050` | error | A fractional delay has neither an explicit unit nor an active timeunit/timescale. |
| `FSIM-SV-SEM-051` | error | A Verilog-2005 delay uses a SystemVerilog explicit physical-unit suffix. |
| `FSIM-SV-SEM-052` | error | A Verilog/SystemVerilog `min:typ:max` delay triple is not parenthesized. |
| `FSIM-SV-SEM-053` | error | A Verilog/SystemVerilog delay list supplies more transition values than the containing assignment or gate form permits. |
| `FSIM-SV-SEM-054` | error | A procedural assignment contains more than one delay or event control. |
| `FSIM-SV-SEM-055` | error | A SystemVerilog type parameter and typedef declare the same type-namespace name in one bounded scope. |
| `FSIM-SV-SEM-056` | error | A SystemVerilog return statement appears outside a function. |
| `FSIM-SV-SEM-058` | error | A bounded function repeats an argument name or conflicts with its result name. |
| `FSIM-SV-SEM-059` | error | A function closing name does not match its declaration name. |
| `FSIM-SV-SEM-060` | error | A bounded function repeats or conflicts with a local declaration. |
| `FSIM-SV-SEM-061` | error | A bounded function assignment target does not have an identifier root. |
| `FSIM-SV-SEM-063` | error | A bounded function assignment is nonblocking or contains a procedural timing/event control. |
| `FSIM-SV-SEM-064` | error | A bounded function contains a timing control, event statement, or task statement. |
| `FSIM-SV-SEM-066` | error | A module or package declares the same bounded function name more than once. |
| `FSIM-SV-SEM-067` | error | A bounded task repeats an argument name. |
| `FSIM-SV-SEM-068` | error | A bounded task closing name differs from its declaration name. |
| `FSIM-SV-SEM-069` | error | A bounded task local conflicts with an argument or earlier local. |
| `FSIM-SV-SEM-071` | error | A bounded task return statement incorrectly supplies a value. |
| `FSIM-SV-SEM-073` | error | A module or package declares the same bounded task name more than once. |
| `FSIM-SV-SEM-075` | error | A bounded text-file system function has the wrong argument count. |
| `FSIM-SV-SEM-076` | error | `$fdisplay` or `$fwrite` has a nonliteral, malformed, unsupported, or multi-value format. |
| `FSIM-SV-SEM-077` | error | A dynamic array, queue, or associative array is used outside SystemVerilog-2017; a one-dimensional static Verilog-2005 memory remains legal. |
| `FSIM-SV-SEM-078` | error | An unpacked declaration has an unsupported dimension or associative index type. |
| `FSIM-SV-SEM-079` | error | A bounded container has an unsupported nonintegral element type. |
| `FSIM-SV-SEM-081` | error | A supported container method has the wrong argument count. |
| `FSIM-SV-SEM-084` | error | An assignment pattern is used outside SystemVerilog-2017. |
| `FSIM-SV-SEM-085` | error | An unpacked-container reduction method is used outside SystemVerilog-2017. |
| `FSIM-SV-SEM-086` | error | An unpacked-container ordering method is used outside SystemVerilog-2017. |
| `FSIM-SV-SEM-087` | error | An unpacked-container locator method is used outside SystemVerilog-2017. |
| `FSIM-SV-SEM-088` | error | A predicate unpacked-container locator method is used outside SystemVerilog-2017. |
| `FSIM-SV-SEM-089` | error | A predicate unpacked-container locator omits its required nonempty `with` clause. |
| `FSIM-SV-SEM-090` | error | A predicate unpacked-container locator has malformed or multiple iterator binders, or a scoped container iterator leaks outside its `with` expression. |
| `FSIM-SV-SEM-091` | error | An unpacked-container reduction has an empty `with` transformation. |
| `FSIM-SV-SEM-092` | error | A `sort` or `rsort` key has an empty `with` clause, a malformed or multiple iterator binder, or a named binder without a `with` clause. |
| `FSIM-SV-SEM-093` | error | An extrema/uniqueness locator transformation has an empty `with` clause, a malformed or multiple iterator binder, or a named binder without a `with` clause. |
| `FSIM-SV-SEM-094` | error | A reduction transformation has a malformed or multiple iterator binder, or a named binder without a `with` clause. |
| `FSIM-SV-SEM-095` | error | A default function actual is attached to a writable or reference formal instead of an input value formal. |
| `FSIM-SV-SEM-096` | error | A `ref` function formal is declared in a static or implicit-lifetime function. |
| `FSIM-SV-SEM-097` | error | A default task actual is attached to a writable or reference formal instead of an input value formal. |
| `FSIM-SV-SEM-098` | error | A `ref` task formal is declared in a static or implicit-lifetime task. |
| `FSIM-SV-SEM-100` | error | A streaming concatenation is used outside SystemVerilog-2017. |
| `FSIM-SV-SEM-101` | error | A bounded `always_ff` process does not have exactly one edge-qualified event. |
| `FSIM-SV-SEM-102` | error | A bounded `always_ff` body contains a nested timing control. |
| `FSIM-SV-UNSUPPORTED-001` | error | Unsupported compilation-unit item. |
| `FSIM-SV-UNSUPPORTED-002` | error | A raw parser input contains a directive that was not consumed by preprocessing. |
| `FSIM-SV-UNSUPPORTED-004` | error | Unsupported module item. |
| `FSIM-SV-UNSUPPORTED-005` | error | A named port connection was used where a module-header declaration is required. |
| `FSIM-SV-UNSUPPORTED-008` | error | Unsupported identifier-starting procedural statement. |
| `FSIM-SV-UNSUPPORTED-009` | error | Unsupported procedural statement. |
| `FSIM-SV-UNSUPPORTED-010` | error | ANSI port default expressions are parsed but not executable. |
| `FSIM-SV-UNSUPPORTED-011` | error | Declaration initializers are parsed but not executable. |
| `FSIM-SV-UNSUPPORTED-014` | error | A procedural declaration uses the net-only `wire` type. |
| `FSIM-SV-UNSUPPORTED-017` | error | A `unique`, `unique0`, or `priority` qualifier is not followed by a case statement. |
| `FSIM-SV-UNSUPPORTED-019` | error | Type parameters are not implemented. |
| `FSIM-SV-UNSUPPORTED-021` | error | A generate region or branch contains an item outside the bounded integral-parameter, local-signal, continuous assignment, process, instance, and nested-generate subset. |
| `FSIM-SV-UNSUPPORTED-022` | error | A generated local declaration incorrectly uses a module-port direction. |
| `FSIM-SV-UNSUPPORTED-023` | error | A package item is outside the bounded integral parameter/localparam and import subset. |
| `FSIM-SV-UNSUPPORTED-024` | error | A bounded typedef target is not an integral built-in or user-defined type. |
| `FSIM-SV-UNSUPPORTED-026` | error | An enum declaration selects a nonintegral base type. |
| `FSIM-SV-UNSUPPORTED-028` | error | A bounded aggregate member uses a data type outside the packed integral, enum, or nested aggregate subset. |
| `FSIM-SV-UNSUPPORTED-029` | error | A bounded packed aggregate member has an unpacked dimension. |
| `FSIM-SV-UNSUPPORTED-041` | error | `reverse` or deterministic `shuffle` uses an excluded container-ordering `with` clause. |
| `FSIM-SV-UNSUPPORTED-042` | error | Reserved non-emitting identity formerly used for deferred `case matches` binding, tagged, and structured patterns. |
| `FSIM-SV-UNSUPPORTED-043` | error | Reserved non-emitting identity formerly used for deferred `case matches` guards. |
| `FSIM-SV-UNSUPPORTED-045` | error | A class member is outside the current property, method, constraint, typedef, or nested-class foundation. |

## Elaboration and SimIR lowering

### Top selection, objects, and executable lowering

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-ELAB-0001` | error | No unique executable top can be inferred; set `project.top` or `--top`. |
| `FSIM-ELAB-0002` | error | The effective programmatic or command-line root list contains an empty target, missing or unsafe alias, duplicate alias, or invalid legacy/list combination. |
| `FSIM-ELAB-001` | error | The requested top-level design unit was not found. |
| `FSIM-ELAB-002` | error | A VHDL architecture has no matching entity. |
| `FSIM-ELAB-003` | error | A qualified top-level target is malformed. |
| `FSIM-ELAB-004` | error | A qualified VHDL top does not name an architecture. |
| `FSIM-ELAB-005` | error | An unqualified top name is ambiguous across its complete effective logical-library scope. |
| `FSIM-ELAB-006` | error | An unqualified top lookup needs one or more configured logical libraries which are unavailable. |
| `FSIM-ELAB-007` | error | A uniquely inferred top-level SystemC factory could not be constructed. |
| `FSIM-ELAB-008` | error | The elaborated design exceeds the dense process-ID space. |
| `FSIM-ELAB-010` | error | A signal has an invalid packed width. |
| `FSIM-ELAB-011` | error | The design exceeds the dense 32-bit signal-ID limit. |
| `FSIM-ELAB-020` | error | A process sensitivity names an unknown signal. |
| `FSIM-ELAB-030` | error | A parsed delay statement contains no delay value. |
| `FSIM-ELAB-031` | error | An assignment target is not a supported packed object, constant bit-select, or constant part-select. |
| `FSIM-ELAB-032` | error | An assignment target names an unknown signal or local variable. |
| `FSIM-ELAB-040` | error | An expression names an unknown identifier. |
| `FSIM-ELAB-041` | error | A literal is malformed or unsupported by executable lowering. |
| `FSIM-ELAB-042` | error | An operator was parsed but has no executable SimIR lowering. |
| `FSIM-ELAB-043` | error | An expression form was parsed but has no executable SimIR lowering. |
| `FSIM-ELAB-045` | error | A VHDL edge predicate appears outside the one supported process-guard form. |
| `FSIM-ELAB-047` | error | Assignment target and expression widths differ. |
| `FSIM-ELAB-048` | error | A VHDL `if` condition does not have scalar Boolean type. |
| `FSIM-ELAB-049` | error | Binary operands have different widths and would require implicit sizing. |
| `FSIM-ELAB-050` | error | Assignment into a two-state target would implicitly lose four- or nine-state values. |
| `FSIM-ELAB-051` | error | A VHDL assertion condition does not have scalar Boolean type. |
| `FSIM-ELAB-052` | error | A local variable has no executable packed width. |
| `FSIM-ELAB-053` | error | A local-variable declaration duplicates another declaration in the same lexical scope. |
| `FSIM-ELAB-054` | error | A local variable initializer has the wrong packed width. |
| `FSIM-ELAB-055` | error | A VHDL rejection limit exceeds its first waveform-element delay during executable lowering. |
| `FSIM-ELAB-056` | error | A local variable assignment is nonblocking. |
| `FSIM-ELAB-ROOT-001` | error | A source expression uses an unsupported cross-root hierarchy shortcut instead of a language-defined root-level global mechanism. |
| `FSIM-ELAB-ROOT-002` | error | The public elaboration root list is empty or contains an empty target or missing or unsafe alias. |
| `FSIM-ELAB-ROOT-003` | error | The public elaboration root list contains a duplicate alias. |
| `FSIM-ELAB-057` | error | A local variable assignment has the wrong packed width. |
| `FSIM-ELAB-058` | error | A local variable initializer or assignment would implicitly lose four- or nine-state values. |
| `FSIM-ELAB-059` | error | A dynamic wait names an unknown signal. |
| `FSIM-ELAB-060` | error | An edge-qualified dynamic wait names a nonscalar signal. |
| `FSIM-ELAB-061` | error | A wildcard process has no readable signal dependency. |
| `FSIM-ELAB-062` | error | A dynamic wildcard event control has no readable signal dependency. |
| `FSIM-ELAB-063` | error | A case-item choice width does not match its selector width. |
| `FSIM-ELAB-064` | error | A conditional-expression condition is not scalar in the bounded executable slice. |
| `FSIM-ELAB-065` | error | Conditional-expression alternatives have different widths. |
| `FSIM-ELAB-066` | error | A VHDL relational comparison mixes explicitly signed and unsigned packed operands without conversion. |
| `FSIM-ELAB-067` | error | A VHDL arithmetic expression mixes explicitly signed and unsigned packed operands without conversion. |
| `FSIM-ELAB-068` | error | A bit/part select does not have constant in-range bounds, a positive indexed width, or a direction compatible with its declared packed range. |
| `FSIM-ELAB-DYNINDEX-001` | error | A dynamic packed selection has a nonconcrete, null, overflowing, or otherwise unrepresentable declared range or base offset. |
| `FSIM-ELAB-DYNINDEX-002` | error | A dynamic packed selection index is not an executable VHDL `integer` value or the common signed 32-bit index representation required by this bounded implementation. |
| `FSIM-ELAB-VHSLICE-001` | error | A dynamic VHDL packed slice lacks a fixed supported width, a concrete signed 32-bit source range, matching direction, or a statically sized assignment value. |
| `FSIM-ELAB-VHSLICE-002` | error | A dynamic VHDL packed slice bound is not an executable signed 32-bit `integer`-family expression. |
| `FSIM-ELAB-VHSLICE-003` | error | A dynamic VHDL packed slice is followed by another assignment-target selection in the bounded one-dimensional composite subset. |
| `FSIM-ELAB-069` | error | A concatenation is empty or has an operand/result width that cannot be inferred or represented. |
| `FSIM-ELAB-070` | error | A dynamic VHDL packed shift or rotate count does not have the executable base `integer` subtype. |
| `FSIM-ELAB-071` | error | A sequential VHDL for-loop initial bound is not locally static. |
| `FSIM-ELAB-072` | error | A sequential VHDL for-loop final bound is not locally static. |
| `FSIM-ELAB-073` | error | A sequential VHDL for loop exceeds the bounded one-million-iteration elaboration limit. |
| `FSIM-ELAB-074` | error | A sequential for-loop body assigns its VHDL implicit constant or statically substituted SystemVerilog index. |
| `FSIM-ELAB-075` | error | A runtime Verilog/SystemVerilog repeat count is not a supported integral value. |
| `FSIM-ELAB-077` | error | A VHDL while condition is not scalar Boolean. |
| `FSIM-ELAB-078` | error | Loop-control HIR reached elaboration without an enclosing loop. |
| `FSIM-ELAB-079` | error | A VHDL wait-until condition is not scalar Boolean. |
| `FSIM-ELAB-080` | error | Targeted loop-control HIR names no enclosing loop. |
| `FSIM-ELAB-081` | error | A case statement reached elaboration with an invalid internal matching mode. |
| `FSIM-ELAB-082` | error | The bounded packed VHDL `abs` operator has a nonsigned operand. |
| `FSIM-ELAB-083` | error | A SystemVerilog `$signed` or `$unsigned` cast does not have exactly one packed argument. |
| `FSIM-ELAB-084` | error | `$isunknown` is used outside SystemVerilog or without exactly one packed argument. |
| `FSIM-ELAB-085` | error | `$bits` is used outside SystemVerilog or without one statically sized packed argument. |
| `FSIM-ELAB-086` | error | A packed array query (`$left`, `$right`, `$low`, `$high`, `$size`, or `$increment`) is used outside SystemVerilog or without one representable one-dimensional packed argument. |
| `FSIM-ELAB-087` | error | `$onehot` or `$onehot0` is used outside SystemVerilog or without exactly one packed argument. |
| `FSIM-ELAB-088` | error | `$countones` is used outside SystemVerilog or without one statically sized packed argument. |
| `FSIM-ELAB-089` | error | `$countbits` lacks a packed expression or at least one constant one-bit `0`, `1`, `X`, or `Z` control. |
| `FSIM-ELAB-090` | error | `$dimensions` or `$unpacked_dimensions` is used outside SystemVerilog or without one statically sized packed argument. |
| `FSIM-ELAB-091` | error | Bounded VHDL packed exponentiation has a dynamic or negative exponent. |
| `FSIM-ELAB-092` | error | A VHDL conditional-assignment condition is not Boolean. |
| `FSIM-ELAB-094` | error | The VHDL `'event` attribute does not name one visible signal. |
| `FSIM-ELAB-095` | error | The VHDL `'last_value` attribute does not name one visible signal. |
| `FSIM-ELAB-096` | error | The VHDL `'last_event` attribute does not name one visible signal. |
| `FSIM-ELAB-097` | error | The bounded zero-duration VHDL `'stable` attribute does not name one visible signal or supplies a duration. |
| `FSIM-ELAB-098` | error | The VHDL `'active` attribute does not name one visible signal. |
| `FSIM-ELAB-VHATTR-001` | error | The VHDL `'last_active` attribute does not name one visible signal or supplies an argument. |
| `FSIM-ELAB-VHATTR-002` | error | The VHDL `'driving` attribute does not name one visible signal or supplies an argument. |
| `FSIM-ELAB-VHATTR-003` | error | The VHDL `'driving_value` attribute does not name one visible signal or supplies an argument. |
| `FSIM-ELAB-VHATTR-004` | error | A VHDL `'stable` implicit signal has an invalid prefix or a nonstatic or negative duration. |
| `FSIM-ELAB-VHATTR-005` | error | A VHDL `'quiet` implicit signal has an invalid prefix or a nonstatic or negative duration. |
| `FSIM-ELAB-VHATTR-006` | error | A VHDL `'transaction` implicit signal has an invalid prefix, supplies an argument, or is invalid in a wait sensitivity. |
| `FSIM-ELAB-VHATTR-007` | error | A VHDL `'delayed` implicit signal has an invalid prefix or a nonstatic or negative duration. |
| `FSIM-ELAB-VHATTR-008` | error | An implicit VHDL signal attribute would exceed the supported process-ID space. |
| `FSIM-ELAB-099` | error | A named-event trigger does not contain a simple event name. |
| `FSIM-ELAB-100` | error | A named-event trigger references an unknown event. |
| `FSIM-ELAB-101` | error | A named-event trigger targets an object not declared as an event. |
| `FSIM-ELAB-102` | error | A formatted-output value expression could not be lowered into SimIR. |
| `FSIM-ELAB-103` | error | A value-sensitive `$monitor` operand is not yet a direct packed-signal reference. |
| `FSIM-ELAB-104` | error | A random system function is used in an unsupported language or with an invalid argument count. |
| `FSIM-ELAB-105` | error | Procedural assignment timing-control HIR has an inconsistent control kind, delay, or event payload. |
| `FSIM-ELAB-106` | error | Procedural update metadata is inconsistent with its normalized expression or captured lvalue. |
| `FSIM-ELAB-107` | error | A bounded fork appears inside a callable whose shared call frame cannot safely outlive the caller. |
| `FSIM-ELAB-108` | error | A postponed `$strobe` operand is not yet a direct packed-signal reference. |
| `FSIM-ELAB-DRV-001` | error | An unresolved variable has multiple process drivers. |
| `FSIM-ELAB-HIER-001` | error | Duplicate elaborated instance path. |
| `FSIM-ELAB-HIER-002` | error | Recursive instantiation was detected. |
| `FSIM-ELAB-DEFPARAM-001` | error | A `defparam` hierarchy index or value cannot be resolved as a locally static SystemVerilog constant. |
| `FSIM-ELAB-DEFPARAM-002` | error | A `defparam` target does not resolve to an elaborated descendant instance. |
| `FSIM-ELAB-DEFPARAM-003` | error | Multiple overrides assign the same parameter on a `defparam` target. |
| `FSIM-ELAB-DEFPARAM-004` | error | A `defparam` path crosses an unsupported language boundary or targets an object that cannot accept parameter overrides. |
| `FSIM-ELAB-SVEXTERN-001` | error | An extern module, interface, or program declaration has no unique definition in its logical library. |
| `FSIM-ELAB-SVEXTERN-002` | error | An extern module, interface, or program header does not exactly match its definition. |
| `FSIM-ELAB-SVCONFIG-001` | error | A SystemVerilog configuration selected as one simulation root does not contain exactly one design top. |
| `FSIM-ELAB-SVCONFIG-002` | error | A SystemVerilog configuration design top is missing or ambiguous. |
| `FSIM-ELAB-SVCONFIG-003` | error | A SystemVerilog configuration rule cannot select a unique target for an instance. |
| `FSIM-ELAB-SVBIND-001` | error | A bound instance collides with an existing child name in the target scope. |
| `FSIM-ELAB-SVBIND-002` | error | A compilation-unit or unit-local bind directive matches no elaborated target. |
| `FSIM-ELAB-INTEGER-001` | error | A specialization-dependent VHDL integer subtype constraint cannot be evaluated. |
| `FSIM-ELAB-INTEGER-002` | error | A VHDL integer subtype constraint is null, outside its named base subtype, or outside fsim's portable signed 32-bit representation. |
| `FSIM-ELAB-INTEGER-003` | error | A locally static VHDL integer value lies outside its assignment target's concrete subtype range. |
| `FSIM-ELAB-INTEGER-004` | error | A VHDL integer-family target is assigned a packed or otherwise noninteger expression without explicit conversion. |
| `FSIM-ELAB-GEN-001` | error | A conditional-generate expression cannot be evaluated for its specialization. |
| `FSIM-ELAB-GEN-002` | error | A loop-generate initial value cannot be evaluated for its specialization. |
| `FSIM-ELAB-GEN-003` | error | A loop-generate continuation condition cannot be evaluated. |
| `FSIM-ELAB-GEN-004` | error | A loop generate exceeds the bounded one-million-iteration elaboration limit. |
| `FSIM-ELAB-GEN-005` | error | A loop-generate iteration expression cannot be evaluated. |
| `FSIM-ELAB-GEN-006` | error | A loop-generate iteration does not advance its variable. |
| `FSIM-ELAB-GEN-007` | error | A nested loop-generate variable shadows an enclosing constant in the bounded executable slice. |
| `FSIM-ELAB-GEN-008` | error | A selection-generate selector cannot be evaluated in its locally static scalar or enumeration domain. |
| `FSIM-ELAB-GEN-009` | error | A selection-generate choice or range bound is not locally static or does not belong to the selector domain. |
| `FSIM-ELAB-GEN-010` | error | Selection-generate choices or ranges overlap, or the alternatives contain duplicate defaults. |
| `FSIM-ELAB-GEN-011` | error | A generated constant or parameter cannot be evaluated in its declaration-order environment. |
| `FSIM-ELAB-GEN-012` | error | A generated constant/parameter violates its bounded scalar subtype, cannot be converted to its governed arbitrary-width packed type, or exceeds the configured materialization limit. |
| `FSIM-ELAB-GEN-013` | error | A VHDL block guard expression has a non-Boolean type. |
| `FSIM-ELAB-VHBLOCK-001` | error | A VHDL block generic map is missing, excessive, duplicated, unknown, misordered, or selects an unavailable default. |
| `FSIM-ELAB-VHBLOCK-002` | error | A VHDL block port map is missing, excessive, duplicated, unknown, misordered, or uses an illegal actual for the formal mode. |
| `FSIM-ELAB-VHBLOCK-003` | error | A VHDL object or block-port alias names an unknown or profile-incompatible signal target. |
| `FSIM-ELAB-PKG-001` | error | A bounded VHDL package import is not `library.package.all` or `library.package.constant`. |
| `FSIM-ELAB-PKG-002` | error | A project VHDL package named by a use clause was not found in the selected library. |
| `FSIM-ELAB-PKG-003` | error | A selected package constant or type named by a use clause does not exist. |
| `FSIM-ELAB-PKG-004` | error | The same bare VHDL constant name is directly visible from multiple imported packages. |
| `FSIM-ELAB-PKG-005` | error | A package constant default cannot be evaluated in declaration order. |
| `FSIM-ELAB-PKG-006` | error | A package constant value violates its bounded scalar subtype. |
| `FSIM-ELAB-PKG-007` | error | Project-package use visibility contains a dependency cycle. |
| `FSIM-ELAB-PKG-008` | error | A selected package constant is not `package.constant` or `library.package.constant`. |
| `FSIM-ELAB-PKG-009` | error | A package named by a selected constant expression was not found in the requested library. |
| `FSIM-ELAB-PKG-010` | error | A selected constant does not exist in the resolved package. |
| `FSIM-ELAB-CTX-001` | error | A bounded context reference is not `library.context`. |
| `FSIM-ELAB-CTX-002` | error | A project context referenced by a library unit or another context was not found. |
| `FSIM-ELAB-CTX-003` | error | Reusable VHDL context visibility contains a dependency cycle. |
| `FSIM-ELAB-SVPKG-001` | error | A SystemVerilog package named by an import or scoped item was not found in the owning library. |
| `FSIM-ELAB-SVPKG-002` | error | A selected or scoped item does not exist in the resolved SystemVerilog package. |
| `FSIM-ELAB-SVPKG-003` | error | The same direct constant name is imported from multiple SystemVerilog packages. |
| `FSIM-ELAB-SVPKG-004` | error | Recursive SystemVerilog package imports contain a visibility cycle. |
| `FSIM-ELAB-SVPKG-005` | error | A package-scoped item is not exactly `package::name`. |
| `FSIM-ELAB-SVPKG-006` | error | A SystemVerilog package constant default cannot be evaluated in declaration order. |
| `FSIM-ELAB-SVPKG-007` | error | A package export has invalid wildcard shape or is not backed by a matching import. |
| `FSIM-ELAB-SVPKG-008` | error | A selective package export does not select an imported declaration. |
| `FSIM-ELAB-SVNETTYPE-001` | error | A SystemVerilog nettype resolution function is not visible with an executable body. |
| `FSIM-ELAB-SVNETTYPE-002` | error | A SystemVerilog nettype resolution function is ambiguous or has conflicting visible bodies. |
| `FSIM-ELAB-SVNETTYPE-003` | error | A SystemVerilog nettype resolution function does not accept one dynamic array of the net base type and return that base type. |
| `FSIM-ELAB-SVNETTYPE-004` | error | A SystemVerilog nettype resolution function uses a body outside the retained deterministic resolution forms. |
| `FSIM-ELAB-SVALIAS-001` | error | A SystemVerilog alias terminal is not a locally static packed-net lvalue, selects an invalid range, or names a variable. |
| `FSIM-ELAB-SVALIAS-002` | error | A SystemVerilog alias terminal does not name a packed net in its declaration region. |
| `FSIM-ELAB-SVALIAS-003` | error | SystemVerilog alias terminals have different bit lengths or incompatible net/data types. |
| `FSIM-ELAB-SVLET-001` | error | A SystemVerilog let invocation has an invalid positional or named argument binding. |
| `FSIM-ELAB-SVLET-002` | error | SystemVerilog let expansion is recursive. |
| `FSIM-ELAB-SVLET-003` | error | A SystemVerilog let invocation omits a formal without a default. |
| `FSIM-ELAB-SVLET-004` | error | SystemVerilog let expansion exceeded its governed per-unit work budget. |
| `FSIM-ELAB-SVLET-006` | error | A selected SystemVerilog package import names a let declaration that is not visible from the package. |
| `FSIM-ELAB-SVTYPE-001` | error | A SystemVerilog user-defined type is not visible in the unit where it is used. |
| `FSIM-ELAB-SVTYPE-002` | error | The same direct type name is imported from multiple SystemVerilog packages. |
| `FSIM-ELAB-SVTYPE-003` | error | Bounded SystemVerilog typedef aliases contain a cycle. |
| `FSIM-ELAB-SVTYPE-004` | error | A SystemVerilog packed struct, union, or enum assignment-like context does not use the same nominal type, a matching explicit cast, or a legal contextual pattern. |
| `FSIM-ELAB-SVTYPE-005` | error | SystemVerilog equality compares packed struct, union, or enum values with different or missing nominal types. |
| `FSIM-ELAB-SVTYPE-006` | error | The SystemVerilog `type` operator is used outside a supported type comparison. |
| `FSIM-ELAB-SVTYPE-007` | error | An ordinary packed union resolves to members with different widths. |
| `FSIM-ELAB-SVPROCESS-001` | error | A process-handle expression is not a zero-argument `process::self()`, `status()`, or `completed()` call on a direct process receiver. |
| `FSIM-ELAB-SVPROCESS-002` | error | A process-handle statement is not a zero-argument `await()` or `kill()` call on a direct process receiver. |
| `FSIM-ELAB-SVDISABLE-001` | error | A procedural `disable` target is not visible as a named sequential or parallel block in the current lexical scope. |
| `FSIM-ELAB-SVSYNC-001` | error | Mailbox or semaphore construction lacks a compatible 64-bit destination or uses more than one count argument. |
| `FSIM-ELAB-SVSYNC-002` | error | A typed mailbox has no finite positive packed element width. |
| `FSIM-ELAB-SVSYNC-003` | error | A mailbox operation has an unsupported method or argument profile. |
| `FSIM-ELAB-SVSYNC-004` | error | A semaphore operation has an unsupported method or key-count argument profile. |
| `FSIM-ELAB-SVLOOP-001` | error | A runtime procedural for-loop inline variable shadows an active local. |
| `FSIM-ELAB-SVLOOP-002` | error | A runtime procedural for-loop condition is not executable as a packed truth value. |
| `FSIM-ELAB-SVEVENT-001` | error | A packed event expression has no readable signal dependencies. |
| `FSIM-ELAB-SVEVENT-002` | error | Packed event-expression HIR is mixed with another event or timeout. |
| `FSIM-ELAB-SVEVENT-003` | error | A packed event expression does not have an executable width from 1 through 64 bits. |
| `FSIM-ELAB-SVEVENT-004` | error | A repeated event-control count is not an executable integral value. |
| `FSIM-ELAB-SVEVENT-005` | error | A `wait_order` statement has no valid named-event operands. |
| `FSIM-ELAB-SVEVENT-006` | error | A `wait_order` operand names no visible event. |
| `FSIM-ELAB-SVEVENT-007` | error | A `wait_order` operand names an object that is not an event. |
| `FSIM-ELAB-SVEVENT-008` | error | The `triggered` property is selected from an object that is not a named event. |
| `FSIM-ELAB-SVEVENT-009` | error | A named-event assignment does not have an event-variable target and an event-variable or `null` source. |
| `FSIM-ELAB-SVEVENT-010` | error | A named-event alias assignment is nonblocking or has a procedural timing control. |
| `FSIM-ELAB-SVDELAY-001` | error | A SystemVerilog delay expression is not a known nonnegative locally constant integral value after specialization. |
| `FSIM-ELAB-SVDELAY-002` | error | A specialized SystemVerilog delay expression overflows 64-bit simulation time after time-unit normalization. |
| `FSIM-ELAB-SVDELAY-003` | error | Combined continuous-assignment and net-declaration transition delays overflow 64-bit simulation time. |
| `FSIM-ELAB-SVDELAY-004` | error | A runtime SystemVerilog delay expression is not an integral, time, real, shortreal, or realtime packed scalar. |
| `FSIM-ELAB-SVIFACE-001` | error | An interface port actual is not a whole scalar or statically indexed interface instance. |
| `FSIM-ELAB-SVIFACE-002` | error | An interface port actual does not name an already elaborated interface instance. |
| `FSIM-ELAB-SVIFACE-003` | error | An interface port actual has the wrong interface type. |
| `FSIM-ELAB-SVIFACE-004` | error | An interface port selects a modport that its interface type does not declare. |
| `FSIM-ELAB-SVIFACE-005` | error | A retained modport member has no elaborated interface signal. |
| `FSIM-ELAB-SVFUNC-001` | error | The visible bounded function set exceeds the representable SimIR call-stack capacity. |
| `FSIM-ELAB-SVFUNC-002` | error | More than one bounded function has the same visible name. |
| `FSIM-ELAB-SVFUNC-003` | error | A bounded function call has the wrong number of arguments. |
| `FSIM-ELAB-SVFUNC-004` | error | A packed function return or formal type does not specialize to a positive executable width. |
| `FSIM-ELAB-SVFUNC-005` | error | A return statement is outside an executable function or lacks a value during lowering. |
| `FSIM-ELAB-SVFUNC-007` | error | The same bare function name is directly visible from multiple imported SystemVerilog packages. |
| `FSIM-ELAB-SVFUNC-008` | error | A bounded SystemVerilog function result or its whole-container destination has an incompatible container kind, element profile, queue bound, or associative index profile. |
| `FSIM-ELAB-SVFUNC-009` | error | A bounded SystemVerilog function container argument has an incompatible kind, element profile, queue bound, or associative index profile. |
| `FSIM-ELAB-SVFUNC-010` | error | Function actual association metadata is inconsistent, positional ordering is illegal, a name is unknown or duplicated, or a required actual/default is missing. |
| `FSIM-ELAB-SVFUNC-011` | error | Malformed HIR presents a nonintegral writable function formal to the bounded execution path. |
| `FSIM-ELAB-SVFUNC-012` | error | A bounded `ref` function actual is not a writable variable target or the function is not automatic. |
| `FSIM-ELAB-VHFUNC-001` | error | An interface-function generic has no retained profile in HIR. |
| `FSIM-ELAB-VHFUNC-002` | error | An interface-function association or selected actual is not same-language VHDL. |
| `FSIM-ELAB-VHFUNC-003` | error | An interface-function actual is not a simple visible function name. |
| `FSIM-ELAB-VHFUNC-004` | error | A required interface-function generic has no actual or default. |
| `FSIM-ELAB-VHFUNC-005` | error | A named function actual is invisible or has no conforming supported profile. |
| `FSIM-ELAB-VHFUNC-006` | error | A named or box-default function actual is ambiguous among conforming visible functions. |
| `FSIM-ELAB-VHFUNC-007` | error | A selected function actual has no executable body. |
| `FSIM-ELAB-VHFUNC-008` | error | A selected function actual is impure in the bounded interface-function subset. |
| `FSIM-ELAB-VHFUNC-009` | error | An interface-function binding conflicts with a child-local function name. |
| `FSIM-ELAB-VHCONV-002` | error | A visible VHDL conversion target has no positive SimIR-representable executable width. |
| `FSIM-ELAB-VHCONV-003` | error | A VHDL conversion operand is not a supported closely related type or would change packed width or state domain. |
| `FSIM-ELAB-VHCONV-004` | error | A VHDL conversion result is incompatible with its contextual type. |
| `FSIM-ELAB-VHOVER-001` | error | A VHDL function call is ambiguous among the visible overloads after result and actual-profile filtering. |
| `FSIM-ELAB-VHOVER-002` | error | A VHDL function call matches no visible overload after result and actual-profile filtering. |
| `FSIM-ELAB-VHOVER-003` | error | Two visible VHDL function declarations have the same callable profile. |
| `FSIM-ELAB-VHOVER-004` | error | A VHDL procedure call is ambiguous among the visible overloads after actual-profile filtering. |
| `FSIM-ELAB-VHOVER-005` | error | A VHDL procedure call matches no visible overload after actual-profile filtering. |
| `FSIM-ELAB-VHOVER-006` | error | Two visible VHDL procedure declarations have the same callable profile. |
| `FSIM-ELAB-VHQUAL-001` | error | A VHDL qualified expression names a type mark that is not visible. |
| `FSIM-ELAB-VHQUAL-002` | error | A VHDL qualification target has no positive SimIR-representable executable width. |
| `FSIM-ELAB-VHQUAL-003` | error | A VHDL qualified expression operand does not have the target base type, exact bounded shape, width, nominal identity, or state domain. |
| `FSIM-ELAB-VHQUAL-004` | error | A VHDL qualified-expression result is incompatible with its contextual type. |
| `FSIM-ELAB-VHLEGAL-001` | error | A VHDL package function body does not conform to any same-designator declaration. |
| `FSIM-ELAB-VHLEGAL-002` | error | A VHDL package function declaration has no conforming body. |
| `FSIM-ELAB-VHLEGAL-003` | error | A VHDL package procedure body does not conform to any same-designator declaration. |
| `FSIM-ELAB-VHLEGAL-004` | error | A VHDL package procedure declaration has no conforming body. |
| `FSIM-ELAB-VHLEGAL-005` | error | A pure VHDL function reads a non-formal signal. |
| `FSIM-ELAB-VHLEGAL-006` | error | A pure VHDL function calls a procedure. |
| `FSIM-ELAB-VHLEGAL-007` | error | A VHDL function formal default does not match its subtype. |
| `FSIM-ELAB-VHLEGAL-008` | error | A VHDL procedure formal default does not match its subtype. |
| `FSIM-ELAB-VHLEGAL-009` | error | A VHDL function contains a wait or calls a suspending procedure. |
| `FSIM-ELAB-VHLEGAL-010` | error | A deferred VHDL package constant has no full declaration in the corresponding package body. |
| `FSIM-ELAB-VHLEGAL-011` | error | A deferred VHDL package constant and its full declaration have nonconforming subtype indications. |
| `FSIM-ELAB-VHLEGAL-012` | error | A package body redeclares a nondeferred constant from the package declaration. |
| `FSIM-ELAB-VHREPORT-001` | error | A VHDL report expression does not have string type. |
| `FSIM-ELAB-VHREPORT-002` | error | A VHDL severity expression does not have severity_level type. |
| `FSIM-ELAB-VHFILE-001` | error | A VHDL scope declares the same file object more than once. |
| `FSIM-ELAB-VHFILE-002` | error | A VHDL file object has no visible retained file type. |
| `FSIM-ELAB-VHFILE-003` | error | A VHDL file declaration logical name is not a string expression. |
| `FSIM-ELAB-VHFILE-004` | error | A VHDL file open kind is not the supported static `read_mode`, `write_mode`, or `append_mode` value. |
| `FSIM-ELAB-VHFILE-005` | error | A VHDL file operation does not name a visible whole file object. |
| `FSIM-ELAB-VHFILE-006` | error | `file_close` does not have exactly one file-object actual. |
| `FSIM-ELAB-VHFILE-007` | error | `file_open` has an invalid actual count or non-string logical name. |
| `FSIM-ELAB-VHFILE-008` | error | Status-form `file_open` does not have a writable `file_open_status` actual. |
| `FSIM-ELAB-VHFILE-009` | error | `endfile` does not have exactly one visible whole file-object actual. |
| `FSIM-ELAB-VHFILE-010` | error | Direct VHDL file `read` or `write` does not have exactly one value actual. |
| `FSIM-ELAB-VHFILE-011` | error | Direct VHDL file I/O uses an element type outside the bounded integer subset. |
| `FSIM-ELAB-VHFILE-012` | error | Direct VHDL file `read` does not target a writable integer variable. |
| `FSIM-ELAB-VHTEXTIO-001` | error | `readline` or `writeline` does not have one text-file and one writable `line` actual. |
| `FSIM-ELAB-VHTEXTIO-002` | error | A selected TextIO `read` or `write` line actual is not writable. |
| `FSIM-ELAB-VHTEXTIO-003` | error | A TextIO `read` or `write` profile has an invalid actual count or no value actual. |
| `FSIM-ELAB-VHTEXTIO-004` | error | A TextIO `read` value is not a writable bounded scalar. |
| `FSIM-ELAB-VHTEXTIO-005` | error | A TextIO `read` value type is outside the bounded integer, Boolean, and bit profiles. |
| `FSIM-ELAB-VHTEXTIO-006` | error | A TextIO `read` `good` actual is not a writable Boolean. |
| `FSIM-ELAB-VHTEXTIO-007` | error | A TextIO `write` justification is not the static `left` or `right` value. |
| `FSIM-ELAB-VHTEXTIO-008` | error | A TextIO `write` field is not a static value in the bounded range 0 through 4096. |
| `FSIM-ELAB-VHTEXTIO-009` | error | A TextIO `write` value type is outside the bounded integer, Boolean, bit, and string profiles. |
| `FSIM-ELAB-VHTIME-001` | error | A VHDL delay or physical-time literal does not have a locally static integral magnitude. |
| `FSIM-ELAB-VHTIME-002` | error | A VHDL physical-time value overflows the bounded signed 64-bit tick representation. |
| `FSIM-ELAB-VHTIME-003` | error | A VHDL physical-time literal is not exactly representable at the selected project resolution. |
| `FSIM-ELAB-VHNAME-001` | error | A VHDL function or type mark is not visible in an expression context. |
| `FSIM-ELAB-VHRESOLVE-001` | error | A VHDL resolution indication names no visible function with an executable body. |
| `FSIM-ELAB-VHRESOLVE-002` | error | A VHDL resolution function does not have the required pure array-input/base-result profile. |
| `FSIM-ELAB-VHRESOLVE-003` | error | A VHDL resolution function is ambiguous or conflicts with another visible body. |
| `FSIM-ELAB-VHRESOLVE-004` | error | A VHDL resolution function body is outside the bounded OR/AND reduction subset. |
| `FSIM-ELAB-VHSTATIC-001` | error | A VHDL scalar or array type attribute or integer-subtype conversion is not locally static. |
| `FSIM-ELAB-VHSTATIC-002` | error | A locally static VHDL type attribute or integer-subtype conversion is outside the representable or declared range. |
| `FSIM-ELAB-VHPROC-001` | error | An interface-procedure generic has no retained profile in HIR. |
| `FSIM-ELAB-VHPROC-002` | error | An interface-procedure association or selected actual is not same-language VHDL. |
| `FSIM-ELAB-VHPROC-003` | error | An interface-procedure actual is not a simple visible procedure name. |
| `FSIM-ELAB-VHPROC-004` | error | A required interface-procedure generic has no actual or default. |
| `FSIM-ELAB-VHPROC-005` | error | A named procedure actual is invisible or has no conforming class, mode, and type profile. |
| `FSIM-ELAB-VHPROC-006` | error | A named or box-default procedure actual is ambiguous among conforming visible procedures. |
| `FSIM-ELAB-VHPROC-007` | error | A selected procedure actual has no executable body. |
| `FSIM-ELAB-VHPROC-008` | error | An interface-procedure actual names a function rather than a procedure. |
| `FSIM-ELAB-VHPROC-009` | error | A selected procedure actual contains timing or a non-variable update. |
| `FSIM-ELAB-VHPROC-011` | error | An interface-procedure binding conflicts with a child-local subprogram name. |
| `FSIM-ELAB-VHPROC-012` | error | A generated or scoped procedure actual is outside the bounded interface-procedure subset. |
| `FSIM-ELAB-VHPROC-014` | error | A VHDL procedure call names no visible procedure. |
| `FSIM-ELAB-VHPROC-015` | error | A named VHDL procedure-call association is unknown or duplicated. |
| `FSIM-ELAB-VHPROC-016` | error | A positional VHDL procedure actual follows a named actual during lowering. |
| `FSIM-ELAB-VHPROC-017` | error | A VHDL procedure call has too many or too few actuals. |
| `FSIM-ELAB-VHPROC-018` | error | A variable-class, output, or inout procedure formal receives a non-writable actual. |
| `FSIM-ELAB-VHPROC-019` | error | The visible VHDL procedure set exceeds the SimIR call-stack capacity. |
| `FSIM-ELAB-VHPROC-020` | error | A VHDL procedure formal has no executable width in the bounded 1–64-bit representation. |
| `FSIM-ELAB-VHPROC-021` | error | A VHDL procedure return is outside a procedure or carries a value. |
| `FSIM-ELAB-VHPKG-001` | error | An interface-package generic has no retained profile, or a nested interface-package formal appears in a bounded generic package template. |
| `FSIM-ELAB-VHPKG-002` | error | An interface-package association crosses a non-VHDL language boundary. |
| `FSIM-ELAB-VHPKG-003` | error | An interface-package actual is not a simple visible package-instance name. |
| `FSIM-ELAB-VHPKG-004` | error | A required interface-package generic has no actual package instance. |
| `FSIM-ELAB-VHPKG-005` | error | A named package-instance actual is not directly visible. |
| `FSIM-ELAB-VHPKG-006` | error | An interface-package actual resolves to a value, type, function, or procedure rather than a package instance. |
| `FSIM-ELAB-VHPKG-007` | error | A package-instance actual specializes a different generic package template. |
| `FSIM-ELAB-VHPKG-008` | error | A package-instance actual does not conform to the interface package's explicit or defaulted generic map. |
| `FSIM-ELAB-VHPKG-009` | error | A generic package template name is malformed or not found in the requested library. |
| `FSIM-ELAB-VHPKG-010` | error | A selected bounded package instance has an incomplete required function or procedure body. |
| `FSIM-ELAB-VHPKG-011` | error | A generated or scoped package actual is outside the bounded interface-package subset. |
| `FSIM-ELAB-VHPKG-012` | error | A generic package template or local package-instance name is ambiguous. |
| `FSIM-ELAB-VHPKG-013` | error | A local package instantiation selects a nongeneric package declaration. |
| `FSIM-ELAB-VHPKG-014` | error | A generic package template is passed without first creating a package instance. |
| `FSIM-ELAB-VHGSUB-001` | error | A generic subprogram template is missing or not directly visible before its instantiation. |
| `FSIM-ELAB-VHGSUB-002` | error | A generic subprogram template is ambiguous locally or across directly visible packages. |
| `FSIM-ELAB-VHGSUB-003` | error | A generic subprogram instantiation selects the wrong subprogram kind or a nongeneric subprogram. |
| `FSIM-ELAB-VHGSUB-004` | error | A selected generic subprogram template has no conforming executable body. |
| `FSIM-ELAB-VHGSUB-006` | error | A generic subprogram template is called before being instantiated. |
| `FSIM-ELAB-VHGSUB-007` | error | A bounded generic subprogram template contains a recursive call. |
| `FSIM-ELAB-VHGSUB-008` | error | A materialized generic subprogram instance conflicts with another callable subprogram. |
| `FSIM-ELAB-VHGSUB-009` | error | A selected or scoped generic subprogram template is outside the bounded directly visible subset. |
| `FSIM-ELAB-VHGSUB-011` | error | A generic subprogram template contains an unsupported interface-package formal. |
| `FSIM-ELAB-VHGSUB-012` | error | Generic subprogram specialization failed to retain its selected template body. |
| `FSIM-ELAB-VHGSUB-013` | error | A generic subprogram declaration and body do not have conforming generic lists and callable profiles. |
| `FSIM-ELAB-VHCONFIG-001` | error | A VHDL configuration design unit has no retained block-configuration payload. |
| `FSIM-ELAB-VHCONFIG-002` | error | A configuration declaration's target entity is missing or ambiguous in its library. |
| `FSIM-ELAB-VHCONFIG-003` | error | A configuration declaration selects a missing architecture of its target entity. |
| `FSIM-ELAB-VHCONFIG-004` | error | A configuration declaration selects an ambiguous architecture. |
| `FSIM-ELAB-VHCONFIG-005` | error | A configuration rule names a component with no component-style instances in the configured region. |
| `FSIM-ELAB-VHCONFIG-006` | error | A configuration label does not select an instance of the stated component. |
| `FSIM-ELAB-VHCONFIG-007` | error | Configuration label, `all`, or `others` rules overlap or occur more than once for one component. |
| `FSIM-ELAB-VHCONFIG-008` | error | A configuration binding has a malformed entity aspect or selects a missing VHDL entity/architecture. |
| `FSIM-ELAB-VHCONFIG-009` | error | A configuration binding selects an ambiguous VHDL entity/architecture. |
| `FSIM-ELAB-VHCONFIG-010` | error | A nested block/generate configuration has a non-static index or does not select an elaborated occurrence. |
| `FSIM-ELAB-VHCONFIG-011` | error | A positional component map remained after component-formal normalization and cannot be composed through a configuration binding map. |
| `FSIM-ELAB-VHCONFIG-012` | error | A configuration port map is not a supported named target-port to component-port mapping. |
| `FSIM-ELAB-VHCONFIG-013` | error | A configuration binding has a malformed configuration aspect or selects a missing configuration declaration. |
| `FSIM-ELAB-VHCONFIG-014` | error | A configuration binding selects an ambiguous configuration declaration. |
| `FSIM-ELAB-VHCONFIG-015` | error | A nested block/generate scope is configured more than once at the same level. |
| `FSIM-ELAB-VHCOMP-001` | error | A component-style instance has no visible bounded component declaration. |
| `FSIM-ELAB-VHCOMP-002` | error | A component-style instance ambiguously matches multiple equally visible component declarations. |
| `FSIM-ELAB-VHCOMP-003` | error | Default component binding finds no same-library VHDL entity or architecture. |
| `FSIM-ELAB-VHCOMP-005` | error | A default or configured component target has a missing or ambiguous entity interface. |
| `FSIM-ELAB-VHCOMP-006` | error | A component generic profile is incompatible with its bound entity generic profile. |
| `FSIM-ELAB-VHCOMP-007` | error | A component port profile, mode, type, or width is incompatible with its bound entity port profile. |
| `FSIM-ELAB-VHCOMP-008` | error | A component generic association is unknown, duplicated, out of order, excessive, or omits a required formal. |
| `FSIM-ELAB-VHCOMP-009` | error | A component port association is unknown, duplicated, out of order, excessive, omits a required formal, or opens a required input without a default. |
| `FSIM-ELAB-VHCOMP-010` | error | A configuration binding map names an unknown component or entity formal. |
| `FSIM-ELAB-VHCOMP-012` | error | No equally visible component overload matches the instance associations, modes, types, or dependent widths. |
| `FSIM-ELAB-VHCOMP-013` | error | A selected component input default is dynamic, malformed, or incompatible with the bounded scalar/vector/enumeration/record/array port type. |
| `FSIM-ELAB-VHCOMP-014` | error | A component generic association selects `<>`, but the selected component formal has no usable default. |
| `FSIM-ELAB-VHCOMP-015` | error | A selected component non-value generic actual cannot be forwarded into the bound entity profile. |
| `FSIM-ELAB-SVTASK-001` | error | A bounded task call names no visible task. |
| `FSIM-ELAB-SVTASK-003` | error | The visible bounded task set exceeds the representable SimIR call-stack capacity. |
| `FSIM-ELAB-SVTASK-004` | error | More than one bounded task has the same visible name. |
| `FSIM-ELAB-SVTASK-005` | error | A bounded task call has the wrong number of arguments. |
| `FSIM-ELAB-SVTASK-006` | error | A packed task formal type does not specialize to a positive executable width. |
| `FSIM-ELAB-SVTASK-007` | error | A task return statement is outside an executable task during lowering. |
| `FSIM-ELAB-SVTASK-009` | error | The same bare task name is directly visible from multiple imported SystemVerilog packages. |
| `FSIM-ELAB-SVTASK-010` | error | A suspending bounded task is called from `final`, `always_comb`, or `always_latch`. |
| `FSIM-ELAB-SVTASK-011` | error | A bounded SystemVerilog task container input or inout actual has an incompatible kind, element profile, queue bound, or associative index profile. |
| `FSIM-ELAB-SVTASK-012` | error | Task actual association metadata is inconsistent, positional ordering is illegal, a name is unknown or duplicated, or a required actual/default is missing. |
| `FSIM-ELAB-SVTASK-013` | error | A bounded `ref` task actual is not a writable variable target or the task is not automatic. |
| `FSIM-ELAB-SVTASK-014` | error | A resolved covergroup sampling actual does not have a positive packed width representable by the runtime register ABI. |
| `FSIM-ELAB-VHTYPE-001` | error | A bounded VHDL named type is not visible in the design unit where it is used. |
| `FSIM-ELAB-VHTYPE-002` | error | Bounded VHDL named type aliases contain a cycle. |
| `FSIM-ELAB-VHTYPE-003` | error | The same VHDL type name is directly visible from multiple packages. |
| `FSIM-ELAB-VHTYPE-004` | error | A selected VHDL package type name is malformed or does not exist. |
| `FSIM-ELAB-VHACCESS-001` | error | VHDL access type declarations contain a recursive designated-subtype cycle. |
| `FSIM-ELAB-VHACCESS-002` | error | A VHDL access declaration does not resolve to exactly one designated subtype. |
| `FSIM-ELAB-VHACCESS-003` | error | A bounded VHDL access type designates a protected type. |
| `FSIM-ELAB-VHACCESS-004` | error | A VHDL access type has an invalid bounded handle or object-capacity representation. |
| `FSIM-ELAB-VHACCESS-005` | error | A VHDL access operation lacks one resolved designated subtype. |
| `FSIM-ELAB-VHACCESS-006` | error | A VHDL allocator's designated subtype lacks a concrete nonempty bounded packed representation. |
| `FSIM-ELAB-VHACCESS-007` | error | A VHDL allocator designates a nine-state object outside the bounded access-object state subset. |
| `FSIM-ELAB-VHACCESS-008` | error | The VHDL `null` access value lacks an access-type context. |
| `FSIM-ELAB-VHACCESS-009` | error | A VHDL access context has an inconsistent bounded handle width. |
| `FSIM-ELAB-VHACCESS-010` | error | A VHDL allocator lacks a resolved access-type context. |
| `FSIM-ELAB-VHACCESS-011` | error | An allocator subtype differs from its access type's designated subtype. |
| `FSIM-ELAB-VHACCESS-012` | error | A qualified VHDL allocator does not supply exactly one initial value. |
| `FSIM-ELAB-VHACCESS-013` | error | A VHDL allocator initializer has the wrong designated-object width. |
| `FSIM-ELAB-VHACCESS-014` | error | A VHDL dereference read or target lacks one access-typed prefix. |
| `FSIM-ELAB-VHACCESS-015` | error | A dereferenced access object is assigned with timing or non-variable semantics. |
| `FSIM-ELAB-VHACCESS-016` | error | A dereferenced assignment has the wrong designated-object width. |
| `FSIM-ELAB-VHACCESS-017` | error | An operator other than equality or inequality is applied to VHDL access values. |
| `FSIM-ELAB-VHACCESS-018` | error | VHDL access equality is given a nonaccess, nonnull operand. |
| `FSIM-ELAB-VHACCESS-019` | error | VHDL access equality compares distinct nominal access types. |
| `FSIM-ELAB-VHACCESS-020` | error | VHDL access assignment is not a same-nominal value or `null`. |
| `FSIM-ELAB-VHACCESS-021` | error | A nonnull process-local VHDL access handle escapes through a signal. |
| `FSIM-ELAB-VHACCESS-022` | error | Explicit access deallocation is requested for a bounded simulation-lifetime object. |
| `FSIM-ELAB-VHPHYSICAL-001` | error | A VHDL physical type lacks its range or primary unit. |
| `FSIM-ELAB-VHPHYSICAL-002` | error | A physical range is nonstatic or outside signed 32-bit primary-unit ticks. |
| `FSIM-ELAB-VHPHYSICAL-003` | error | A physical primary unit incorrectly carries a secondary-unit scale. |
| `FSIM-ELAB-VHPHYSICAL-004` | error | A secondary physical unit has a nonpositive, nonstatic, forward, or overflowing scale. |
| `FSIM-ELAB-VHPHYSICAL-005` | error | A bounded physical value does not use the signed 32-bit runtime representation. |
| `FSIM-ELAB-VHPHYSICAL-006` | error | A physical literal uses an unknown unit, nonstatic magnitude, overflow, or out-of-range value. |
| `FSIM-ELAB-VHPHYSICAL-007` | error | An unsupported operator is applied to a bounded physical value. |
| `FSIM-ELAB-VHPHYSICAL-008` | error | A physical expression combines distinct nominal physical types. |
| `FSIM-ELAB-VHPHYSICAL-009` | error | Assignment mixes distinct nominal physical types. |
| `FSIM-ELAB-VHPHYSICAL-010` | error | A physical target receives an untyped nonphysical value without explicit conversion. |
| `FSIM-ELAB-VHPROTECTED-001` | error | A protected body has no visible protected type declaration. |
| `FSIM-ELAB-VHPROTECTED-002` | error | A public protected function has no conforming body. |
| `FSIM-ELAB-VHPROTECTED-003` | error | A protected function body has no conforming public profile. |
| `FSIM-ELAB-VHPROTECTED-004` | error | A public protected procedure has no conforming body. |
| `FSIM-ELAB-VHPROTECTED-005` | error | A protected procedure body has no conforming public profile. |
| `FSIM-ELAB-VHPROTECTED-006` | error | A protected type declaration has no body. |
| `FSIM-ELAB-VHPROTECTED-007` | error | A protected private variable lacks a bounded supported scalar or packed type. |
| `FSIM-ELAB-VHPROTECTED-008` | error | A VHDL-2000-or-later shared variable does not have a protected type. |
| `FSIM-ELAB-VHPROTECTED-009` | error | A shared protected object lacks one conforming protected body. |
| `FSIM-ELAB-VHPROTECTED-010` | error | A shared protected object incorrectly carries an object initializer. |
| `FSIM-ELAB-VHPROTECTED-011` | error | A protected private initializer is nonstatic or incompatible with its member. |
| `FSIM-ELAB-VHPROTECTED-012` | error | A protected function call matches no public profile. |
| `FSIM-ELAB-VHPROTECTED-013` | error | A protected function call ambiguously matches multiple public profiles. |
| `FSIM-ELAB-VHPROTECTED-014` | error | A protected method re-enters protected execution. |
| `FSIM-ELAB-VHPROTECTED-015` | error | A bounded protected function is not a single direct value return. |
| `FSIM-ELAB-VHPROTECTED-016` | error | A protected function result is incompatible with its expression context. |
| `FSIM-ELAB-VHPROTECTED-017` | error | Protected private member storage is unavailable or has an invalid bounded width. |
| `FSIM-ELAB-VHPROTECTED-018` | error | A protected procedure call matches no supported public input profile. |
| `FSIM-ELAB-VHPROTECTED-019` | error | A protected procedure call ambiguously matches multiple public profiles. |
| `FSIM-ELAB-VHPROTECTED-020` | error | A protected method attempts to suspend. |
| `FSIM-ELAB-VHPROTECTED-021` | error | A protected method makes a nested procedure call outside the bounded non-reentrant policy. |
| `FSIM-ELAB-VHPROTECTED-022` | error | A pure VHDL function calls an impure protected function. |
| `FSIM-ELAB-VHPROTECTED-023` | error | A legacy VHDL-1993 unprotected shared variable lacks bounded executable storage or a compatible static initializer. |
| `FSIM-ELAB-VHAGG-001` | error | A VHDL aggregate appears without a supported contextual record or array target type. |
| `FSIM-ELAB-VHAGG-002` | error | A contextual record layout or aggregate-association HIR payload is internally inconsistent. |
| `FSIM-ELAB-VHAGG-003` | error | A named aggregate association does not name an element of the contextual record type. |
| `FSIM-ELAB-VHAGG-004` | error | A record aggregate assigns an element more than once, has too many positional associations, or repeats `others`. |
| `FSIM-ELAB-VHAGG-005` | error | A record aggregate omits an element without supplying `others`. |
| `FSIM-ELAB-VHAGG-006` | error | A record aggregate element value does not have the element's exact packed width. |
| `FSIM-ELAB-VHAGG-007` | error | A record aggregate would implicitly lose four- or nine-state information in a two-state element. |
| `FSIM-ELAB-VHAGG-008` | error | A record aggregate choice is empty, names `others` with another choice, or uses a discrete/range form instead of element names. |
| `FSIM-ELAB-VHAGG-009` | error | A record aggregate element value has an incompatible contextual subtype or state domain. |
| `FSIM-ELAB-VHRECORD-001` | error | A VHDL record element does not resolve to a concrete bounded packed scalar, vector, enumeration, array, or nested-record layout. |
| `FSIM-ELAB-VHRECORD-002` | error | A VHDL record element or total recursive record layout overflows the bounded packed representation. |
| `FSIM-ELAB-VHCOMPOP-001` | error | VHDL composite comparison operands do not share one nominal record or array base and compatible element profile, or lack bounded executable widths. |
| `FSIM-ELAB-VHCOMPOP-002` | error | A VHDL composite assignment source has an incompatible nominal base, element profile, rank, or dimension length. |
| `FSIM-ELAB-VHCOMPOP-003` | error | A VHDL concatenation lacks a bounded one-dimensional array context or has incompatible operands, element profiles, state domains, or result length. |
| `FSIM-ELAB-VHCOMPOP-004` | error | A record member in an interleaved VHDL array/record selection chain is unknown or has no bounded executable read or assignment layout. |
| `FSIM-ELAB-VHSUBTYPE-001` | error | A scalar `range` constraint is applied to a resolved noninteger base subtype. |
| `FSIM-ELAB-VHSUBTYPE-002` | error | A derived integer subtype constraint lies outside its resolved base subtype. |
| `FSIM-ELAB-VHSUBTYPE-003` | error | A packed index constraint is applied to a scalar, record, or otherwise nonarray base subtype. |
| `FSIM-ELAB-VHSUBTYPE-004` | error | A constrained packed-array subtype is constrained again. |
| `FSIM-ELAB-VHARRAY-001` | error | A VHDL array element subtype is missing, indefinite, zero-width, integer, string, unknown, or otherwise outside the bounded packed runtime. |
| `FSIM-ELAB-VHARRAY-003` | error | A VHDL array constraint lies outside its `integer`, `natural`, or `positive` index subtype. |
| `FSIM-ELAB-VHARRAY-004` | error | A VHDL array constraint width overflows the packed runtime representation. |
| `FSIM-ELAB-VHARRAY-005` | error | A VHDL array object uses an unconstrained or otherwise nonconcrete array subtype. |
| `FSIM-ELAB-VHEXTERNAL-001` | error | A VHDL external signal name has malformed HIR, names no visible signal, or declares a subtype incompatible with its target. |
| `FSIM-ELAB-VHARRAY-006` | error | Assignment or comparison mixes values from different nominal VHDL array types. |
| `FSIM-ELAB-VHARRAY-007` | error | An operator other than equality, inequality, or matching equality is applied to a VHDL array value in the current bounded semantic path. |
| `FSIM-ELAB-VHARRAY-008` | error | The number of constraints on a VHDL array subtype indication does not match the base array rank. |
| `FSIM-ELAB-VHARRAYAGG-002` | error | Contextual VHDL array layout or aggregate-choice HIR metadata is inconsistent with the aggregate value. |
| `FSIM-ELAB-VHARRAYAGG-003` | error | A VHDL array aggregate choice is nonstatic or outside the contextual index range. |
| `FSIM-ELAB-VHARRAYAGG-004` | error | A VHDL array aggregate covers an index more than once, has too many positional values, or repeats `others`. |
| `FSIM-ELAB-VHARRAYAGG-005` | error | A VHDL array aggregate leaves a contextual index uncovered without supplying `others`. |
| `FSIM-ELAB-VHARRAYAGG-006` | error | A VHDL array aggregate element has the wrong packed width. |
| `FSIM-ELAB-VHARRAYAGG-007` | error | A VHDL array aggregate would implicitly lose four- or nine-state information in a two-state element. |
| `FSIM-ELAB-VHARRAYAGG-008` | error | A VHDL array aggregate combines `others` with another choice in the same association. |
| `FSIM-ELAB-VHARRAYAGG-009` | error | A VHDL array aggregate element value has an incompatible contextual subtype or state domain. |
| `FSIM-ELAB-VHARRAYSEL-001` | error | A multidimensional or composite VHDL array selection has no concrete executable packed layout or representable shape. |
| `FSIM-ELAB-VHARRAYSEL-002` | error | A runtime multidimensional VHDL array index is not an integer-family signed 32-bit value with representable bounds. |
| `FSIM-ELAB-VHARRAYSEL-003` | error | A multidimensional VHDL array read or target index is outside its selected source dimension. |
| `FSIM-ELAB-VHARRAYSEL-004` | error | A multidimensional VHDL array slice has incompatible direction, bounds, placement, or contextual shape. |
| `FSIM-ELAB-VHARRAYATTR-001` | error | A VHDL array attribute prefix is unknown, nonarray, unconstrained, null, or otherwise lacks a concrete bounded range. |
| `FSIM-ELAB-VHARRAYATTR-002` | error | A VHDL array attribute selects a nonstatic dimension or a dimension outside the concrete array rank. |
| `FSIM-ELAB-VHARRAYATTR-003` | error | VHDL `range` or `reverse_range` is used as a scalar expression rather than a discrete range. |
| `FSIM-ELAB-VHARRAYATTR-004` | error | A scalar VHDL array attribute result is outside the portable signed 32-bit integer representation. |
| `FSIM-ELAB-VHDLMATCH-001` | error | A matching case or selected assignment is outside VHDL-2008 or has a selector outside the bounded bit/std_ulogic scalar or one-dimensional-array domain. |
| `FSIM-ELAB-VHDLMATCH-002` | error | A bounded matching choice is not a locally static bit/std_ulogic literal of the selector width. |
| `FSIM-ELAB-VHDLMATCH-003` | error | Two matching choices overlap after applying `-` wildcard and 0/L or 1/H equivalence. |
| `FSIM-ELAB-VHDLMATCH-004` | error | Matching equality operands are outside the bit/std_ulogic scalar or one-dimensional-array domain. |
| `FSIM-ELAB-VHDLCASE-001` | error | A VHDL case choice or range bound is not a locally static value of the selector's discrete or packed literal type. |
| `FSIM-ELAB-VHDLCASE-002` | error | A VHDL case choice or range lies outside the selector subtype constraint. |
| `FSIM-ELAB-VHDLCASE-003` | error | A VHDL case statement repeats a discrete or packed choice. |
| `FSIM-ELAB-VHDLCASE-004` | error | Two VHDL case choices or nonnull ranges overlap. |
| `FSIM-ELAB-VHDLCASE-005` | error | A VHDL case statement neither covers its complete selector subtype nor supplies `others`. |
| `FSIM-ELAB-VHDLCASE-006` | error | Validated VHDL case-range HIR is malformed or lost its static bounds before lowering. |
| `FSIM-ELAB-VHDLGUARD-001` | error | A guarded concurrent assignment has no enclosing Boolean block guard or no retained assignment leaf. |
| `FSIM-ELAB-VHDLGUARD-002` | error | A guarded driver disconnection targets a local or non-nine-state signal. |
| `FSIM-ELAB-VHENUM-001` | error | A contextual VHDL enumeration type has no matching identifier or character literal. |
| `FSIM-ELAB-VHENUM-002` | error | Assignment or comparison mixes values from different nominal VHDL enumeration types. |
| `FSIM-ELAB-VHENUM-003` | error | An operator that is not defined for VHDL enumeration values was applied to an enumeration object. |
| `FSIM-ELAB-VHENUMATTR-001` | error | A VHDL enumeration scalar attribute has an invalid prefix, arity, argument type, result context, or executable ordinal range. |
| `FSIM-ELAB-VHENUMATTR-002` | error | A locally static or executable VHDL enumeration scalar attribute argument is outside the type's declaration range or has no predecessor/successor. |
| `FSIM-ELAB-VHSCALARATTR-001` | error | A non-enumeration VHDL scalar attribute has an invalid type-mark prefix, arity, argument type, or executable range. |
| `FSIM-ELAB-VHSCALARATTR-002` | error | A locally static or executable VHDL scalar attribute argument is outside its subtype range or has no predecessor/successor. |
| `FSIM-ELAB-VHSCALARATTR-003` | error | A scalar VHDL `range` or `reverse_range` attribute is used as a scalar expression rather than a discrete range. |
| `FSIM-ELAB-VHENUMRANGE-001` | error | A VHDL enumeration subtype constraint bound is not locally static, is unknown, or belongs to a different nominal enumeration type. |
| `FSIM-ELAB-VHENUMRANGE-002` | error | A VHDL enumeration subtype constraint is null or lies outside the base enumeration's literal table. |
| `FSIM-ELAB-VHENUMRANGE-003` | error | A derived VHDL enumeration subtype constraint is not contained by its resolved base subtype. |
| `FSIM-ELAB-VHENUMRANGE-004` | error | A locally static value assigned to a constrained VHDL enumeration object lies outside the subtype range. |
| `FSIM-ELAB-VHPORT-001` | error | A VHDL input-port expression is not a supported locally static value for its contextual formal type, or a nonstatic expression requires VHDL-2008 and an explicitly driven intermediate signal under the selected older revision. |
| `FSIM-ELAB-VHPORT-002` | error | A VHDL output, buffer, or inout port is associated with an expression that is not a writable signal name. |
| `FSIM-ELAB-VITAL-001` | error | A compiler-supplied VITAL constant is used without its exact concrete delay or map context. |
| `FSIM-ELAB-VITAL-002` | error | A scalar VITAL primitive is used in a nonscalar result context. |
| `FSIM-ELAB-VITAL-003` | error | A VITAL function call has a missing, excessive, duplicate, unknown, or misplaced actual. |
| `FSIM-ELAB-VITAL-004` | error | A VITAL primitive data, select, or enable actual is not a compatible standard-logic scalar or vector. |
| `FSIM-ELAB-VITAL-005` | error | A caller-supplied VITAL result, result-Z, or output map has the wrong type or layout. |
| `FSIM-ELAB-VITAL-006` | error | A VITAL delay function has an unsupported, unconstrained, or incompatible delay profile or cannot recover its TIME element type. |
| `FSIM-ELAB-VITAL-007` | error | A VITAL mux or decoder has null, incompatible, non-power-of-two, or select-space-exceeding dimensions. |
| `FSIM-ELAB-VITAL-008` | error | A VITAL truth table is dynamic, empty, dimensionally inconsistent, or lacks a concrete input/result layout. |
| `FSIM-ELAB-VITAL-009` | error | A VITAL truth-table row contains a symbol that is illegal in its input or output portion. |
| `FSIM-ELAB-VITAL-010` | error | A VITAL timing-check call has a missing, excessive, duplicate, unknown, or misplaced actual. |
| `FSIM-ELAB-VITAL-011` | error | A VITAL timing-check limit, delay, Boolean, edge, string, or severity actual is not locally static or lies outside its legal profile. |
| `FSIM-ELAB-VITAL-012` | error | A VITAL timing-check violation, state, signal, or trigger actual is not a compatible writable scalar, vector, or record object. |
| `FSIM-ELAB-VITAL-013` | error | A `VitalStateTable` call has a missing, excessive, duplicate, unknown, or misplaced actual. |
| `FSIM-ELAB-VITAL-014` | error | A `VitalStateTable` result, previous-input, or data actual is not a compatible writable standard-logic object with a concrete profile. |
| `FSIM-ELAB-VITAL-015` | error | A `VitalStateTable` table, row width, state count, or static dimension is empty, dynamic, or inconsistent with its selected profile. |
| `FSIM-ELAB-VITAL-016` | error | A `VitalStateTable` row contains a symbol that is illegal in its input, present-state, or output portion. |
| `FSIM-ELAB-VITAL-017` | error | A VITAL signal, wire, or path-delay call has an excessive, duplicate, unknown, or misplaced actual. |
| `FSIM-ELAB-VITAL-018` | error | A VITAL path-delay mode, Boolean control, output name, severity, or related static control actual is invalid. |
| `FSIM-ELAB-VITAL-019` | error | A VITAL delay output/input signal, `OutTemp`, or writable `VitalGlitchDataType` variable has an incompatible scalar or nominal profile. |
| `FSIM-ELAB-VITAL-020` | error | A VITAL delay or output-map actual has an incompatible nominal layout, width, state domain, or negative delay value. |
| `FSIM-ELAB-VITAL-021` | error | A VITAL path array is dynamic or malformed, uses invalid static choices, or contains an incompatible path record. |
| `FSIM-ELAB-VITALMEM-001` | error | A `VitalDeclareMemory` call has an incompatible result context or a missing, excessive, duplicate, unknown, or misplaced geometry/load actual. |
| `FSIM-ELAB-VITALMEM-002` | error | `VitalDeclareMemory` geometry is not static and positive, or its subword is wider than its word. |
| `FSIM-ELAB-VITALMEM-003` | error | A `VitalDeclareMemory` load filename or binary-format control has an incompatible string/Boolean profile. |
| `FSIM-ELAB-VITALMEM-004` | error | A static `VitalDeclareMemory` load file is unavailable, unreadable, or exceeds the bounded input budget. |
| `FSIM-ELAB-SVENUM-001` | error | An enum base width is non-positive or exceeds the governed constant-width limit, or an arbitrary-width enumerator value does not fit it. |
| `FSIM-ELAB-SVENUM-002` | error | Two literals in one bounded enum have the same value. |
| `FSIM-ELAB-SVCONST-001` | error | A SystemVerilog parameter value cannot be converted to its declared bounded integral type without losing X/Z state or valid width metadata. |
| `FSIM-ELAB-SVEXIT-001` | error | `$exit` is used outside a SystemVerilog program block. |
| `FSIM-ELAB-SVSAMPLE-001` | error | A sampled/global-clock value function has an invalid direct packed signal, explicit scalar clock, scalar gate, or governed positive constant history depth. |
| `FSIM-ELAB-SVEXPR-001` | error | Streaming-concatenation HIR has the wrong language, arity, or operand shape. |
| `FSIM-ELAB-SVEXPR-002` | error | A streaming-concatenation slice size is not a positive locally constant host-addressable value. |
| `FSIM-ELAB-SVEXPR-003` | error | A streaming concatenation has a container, aggregate, or unknown-width operand instead of a fixed-width packed integral operand. |
| `FSIM-ELAB-SVEXPR-004` | error | A runtime-base packed part-select width is not a positive locally constant value in the SimIR execution-width representation. |
| `FSIM-ELAB-SVEXPR-006` | error | A runtime-base packed part-select target is used with an unsupported assignment kind. |
| `FSIM-ELAB-SVEXPR-007` | error | Update-expression HIR is not a supported SystemVerilog prefix or postfix increment/decrement of one writable operand. |
| `FSIM-ELAB-SVEXPR-008` | error | A runtime-selected packed procedural target has a following selection. |
| `FSIM-ELAB-SVFORCE-001` | error | A procedural force or release target is not a supported signal, static bit-select, static part-select, or packed member. |
| `FSIM-ELAB-SVFORCE-002` | error | A procedural force or release target is not a visible packed signal with an executable layout. |
| `FSIM-ELAB-SVFORCE-003` | error | A four-state value is forced onto a two-state target without explicit conversion. |
| `FSIM-ELAB-VHFORCE-001` | error | A VHDL force or release target is not a supported signal, static index, or static slice. |
| `FSIM-ELAB-VHFORCE-002` | error | A VHDL force or release target is not a visible packed signal with an executable layout. |
| `FSIM-ELAB-VHFORCE-003` | error | A four- or nine-state VHDL value is forced onto a two-state target without explicit conversion. |
| `FSIM-ELAB-SVFILE-001` | error | A Verilog/SystemVerilog text-file handle is not a 32-bit integer expression. |
| `FSIM-ELAB-SVFILE-002` | error | A file read/error target is not a whole writable packed or string variable. |
| `FSIM-ELAB-SVFILE-003` | error | `$fopen` does not have compatible Verilog byte-string filename and mode operands. |
| `FSIM-ELAB-SVFILE-004` | error | `$fgets` does not have a writable packed or string target and integer handle. |
| `FSIM-ELAB-SVFILE-005` | error | `$feof` does not have one integer handle. |
| `FSIM-ELAB-SVFILE-006` | error | `$ferror` does not have an integer handle and writable packed or string target. |
| `FSIM-ELAB-SVFILE-007` | error | A bounded formatted file-output value cannot be lowered, or a postponed file-strobe/file-monitor operand is not a direct packed signal. |
| `FSIM-ELAB-SVFILE-008` | error | A module integer initializer is not a known 32-bit constant. |
| `FSIM-ELAB-SVFILE-009` | error | `$fgetc` does not have one integer file handle. |
| `FSIM-ELAB-SVFILE-010` | error | `$ungetc` does not have a character expression and integer file handle. |
| `FSIM-ELAB-SVFILE-011` | error | `$fscanf` or `$sscanf` has an invalid source, format, arity, or bounded conversion list. |
| `FSIM-ELAB-SVFILE-012` | error | A formatted scan target is not a direct writable compatible packed or string variable. |
| `FSIM-ELAB-SVFILE-013` | error | `$fread` does not have a compatible integer handle and optional bounded start/count expressions. |
| `FSIM-ELAB-SVFILE-014` | error | A binary-read target is not a direct writable positive-width packed value or one-dimensional fixed integral memory. |
| `FSIM-ELAB-SVFILE-015` | error | `$fseek`, `$ftell`, or `$rewind` has an invalid handle, arity, offset, or origin expression. |
| `FSIM-ELAB-SVFILE-016` | error | `$fflush` has a nonintegral explicit file handle. |
| `FSIM-ELAB-SVCLI-001` | error | `$test$plusargs` or `$value$plusargs` has an invalid language, arity, or string query operand. |
| `FSIM-ELAB-SVCLI-002` | error | `$value$plusargs` does not have one literal, valid, nonsuppressed assignment conversion. |
| `FSIM-ELAB-SVCLI-003` | error | A `$value$plusargs` target is not a direct writable variable compatible with the selected conversion. |
| `FSIM-ELAB-SVSYS-001` | error | `$system` is used outside SystemVerilog or with more than one argument or a nonstring command. |
| `FSIM-ELAB-SVQUEUE-001` | error | A stochastic queue task/function has the wrong positional arity or a nonintegral/nonwritable argument profile. |
| `FSIM-ELAB-SVPLA-001` | error | A PLA task has the wrong positional profile, a nonascending/nonfixed personality memory, mismatched input/output widths, or an asynchronous invocation that would escape a callable frame. |
| `FSIM-ELAB-SVVCD-001` | error | A four-state or extended VCD control task has the wrong language, arity, argument form, filename, or hierarchy-selection profile. |
| `FSIM-ELAB-SVMATH-001` | error | A scalar math system function is used outside SystemVerilog or with the wrong arity. |
| `FSIM-ELAB-SVMATH-002` | error | A scalar math or bit-preserving conversion operand has an incompatible type or no representable executable width. |
| `FSIM-ELAB-SVTIME-001` | error | `$timeformat` is used outside SystemVerilog or without integral units, precision, and minimum-width operands plus a string suffix. |
| `FSIM-ELAB-SVTIME-002` | error | `$printtimescale` is used outside SystemVerilog or with more than one, named, or non-hierarchical argument. |
| `FSIM-ELAB-SVTIME-003` | error | `$time`, `$stime`, or `$realtime` is used outside SystemVerilog or with arguments. |
| `FSIM-ELAB-SVCOV-001` | error | `$get_coverage` or `$get_inst_coverage` is used outside SystemVerilog or with arguments. |
| `FSIM-ELAB-SVCOV-002` | error | `$set_coverage_db_name` or `$load_coverage_db` is used outside SystemVerilog or without exactly one positional filename expression. |
| `FSIM-ELAB-SVCONTAINER-001` | error | A container local is duplicated in one automatic scope. |
| `FSIM-ELAB-SVCONTAINER-002` | error | A container element expression has no resolvable element type. |
| `FSIM-ELAB-SVCONTAINER-003` | error | A container element is a string, unpacked aggregate, unresolved, or lacks a positive executable scalar, enum, or packed-aggregate width. |
| `FSIM-ELAB-SVCONTAINER-004` | error | A bounded queue maximum index is unknown or negative. |
| `FSIM-ELAB-SVCONTAINER-005` | error | A container value is not a direct supported object reference. |
| `FSIM-ELAB-SVCONTAINER-006` | error | A container expression references an unknown container object. |
| `FSIM-ELAB-SVCONTAINER-007` | error | A container method receiver is not a direct supported object. |
| `FSIM-ELAB-SVCONTAINER-008` | error | A container uses an unsupported method. |
| `FSIM-ELAB-SVCONTAINER-009` | error | A container assignment is nonblocking or has a timing/event control. |
| `FSIM-ELAB-SVCONTAINER-010` | error | A whole-container assignment is neither `new[size]` nor a compatible container value. |
| `FSIM-ELAB-SVCONTAINER-011` | error | A container target is neither a whole object nor one element index. |
| `FSIM-ELAB-SVCONTAINER-012` | error | A module container uses a declaration initializer instead of an initial block. |
| `FSIM-ELAB-SVCONTAINER-013` | error | An associative-array index is unresolved, nonintegral, unpacked, handle-valued, or outside the executable host metadata width. Packed integral scalar, enum, struct, and union indices are accepted without a 64-bit language cap. |
| `FSIM-ELAB-SVCONTAINER-014` | error | `new[size]` is used to resize an associative array. |
| `FSIM-ELAB-SVCONTAINER-015` | error | An associative-array query or traversal method is used on another container kind. |
| `FSIM-ELAB-SVCONTAINER-016` | error | An associative-array traversal argument is not a direct mutable integral variable. |
| `FSIM-ELAB-SVCONTAINER-017` | error | An associative-array traversal argument width does not match the index type. |
| `FSIM-ELAB-SVCONTAINER-018` | error | `delete(index)` is used on a container other than a queue or associative array. |
| `FSIM-ELAB-SVCONTAINER-019` | error | A queue-only insert, push, or pop method is used on another container kind. |
| `FSIM-ELAB-SVCONTAINER-020` | error | Static unpacked-array bounds are not locally constant signed 32-bit values that fit the per-container owning-storage budget. |
| `FSIM-ELAB-SVCONTAINER-021` | error | `delete()` is used to clear a fixed static unpacked array. |
| `FSIM-ELAB-SVCONTAINER-022` | error | A mutating container method is applied to a temporary or another non-object receiver. |
| `FSIM-ELAB-SVCONTAINER-023` | error | A dynamic-array `new[size](initializer)` value is not an exactly compatible dynamic array. |
| `FSIM-ELAB-SVCONTAINER-024` | error | Selected assignment targets a string, nested-container, or unpacked-aggregate element without a typed composite element operation. |
| `FSIM-ELAB-SVCOND-001` | error | A container conditional has invalid arity or is used outside SystemVerilog. |
| `FSIM-ELAB-SVCOND-002` | error | Container conditional alternatives do not have an exactly compatible kind and profile. |
| `FSIM-ELAB-SVCOND-003` | error | An associative-array conditional value is used outside the bounded consumer subset. |
| `FSIM-ELAB-SVCOND-004` | error | A container conditional condition does not produce one bit. |
| `FSIM-ELAB-SVEQUAL-001` | error | A container operand is used with an unsupported comparison operator or outside SystemVerilog. |
| `FSIM-ELAB-SVEQUAL-002` | error | Whole-container equality does not have two typed container operands. |
| `FSIM-ELAB-SVEQUAL-003` | error | Whole-container equality operands do not have an exactly compatible kind and profile. |
| `FSIM-ELAB-SVMEMBER-001` | error | An `inside` membership expression is lowered outside SystemVerilog. |
| `FSIM-ELAB-SVMEMBER-002` | error | An `inside` expression does not contain a left operand and at least one member. |
| `FSIM-ELAB-SVMEMBER-003` | error | The left operand of bounded `inside` membership is not a scalar integral value with statically inferable width. |
| `FSIM-ELAB-SVMEMBER-004` | error | An `inside` member is a nested set, unpacked container, string, aggregate, concatenation, or another unsupported nonintegral value. |
| `FSIM-ELAB-SVMEMBER-005` | error | An `inside` value or range bound does not exactly match the left operand width and signedness. |
| `FSIM-ELAB-SVMEMBER-006` | error | Internal `inside` range metadata does not contain exactly one low and high bound. |
| `FSIM-ELAB-SVCASEINSIDE-001` | error | A `case inside` statement is lowered outside SystemVerilog. |
| `FSIM-ELAB-SVCASEINSIDE-002` | error | A bounded `case inside` selector is not scalar integral or its width is not statically inferable. |
| `FSIM-ELAB-SVCASEINSIDE-003` | error | A `case inside` choice is a nested membership expression, range marker in value position, container, string, aggregate, concatenation, or another unsupported nonintegral value. |
| `FSIM-ELAB-SVCASEINSIDE-004` | error | A `case inside` value or range bound does not exactly match the selector width and signedness. |
| `FSIM-ELAB-SVCASEINSIDE-005` | error | Internal `case inside` HIR contains an alternative without a choice. |
| `FSIM-ELAB-SVCASEINSIDE-006` | error | Internal `case inside` range metadata does not contain exactly one low and high bound. |
| `FSIM-ELAB-SVCASEQUAL-001` | error | Internal case-statement HIR contains an invalid qualifier. |
| `FSIM-ELAB-SVCASEQUAL-002` | error | A qualified case statement is lowered outside SystemVerilog. |
| `FSIM-ELAB-SVMATCH-001` | error | A `case matches` statement is lowered outside SystemVerilog. |
| `FSIM-ELAB-SVMATCH-002` | error | A `case matches` selector is not packed integral or its width is not statically inferable. |
| `FSIM-ELAB-SVMATCH-003` | error | A `case matches` item is not an integral constant, wildcard, binding, tagged, or structured pattern. |
| `FSIM-ELAB-SVMATCH-004` | error | Reserved non-emitting identity formerly used when constant patterns required the selector's exact width and signedness. |
| `FSIM-ELAB-SVMATCH-005` | error | Internal `case matches` HIR contains an item without exactly one pattern. |
| `FSIM-ELAB-SVMATCH-006` | error | A `case matches` guard is not a valid packed condition. |
| `FSIM-ELAB-SVMATCH-007` | error | A `case matches` binding is unnamed or binds one name more than once in a pattern. |
| `FSIM-ELAB-SVMATCH-008` | error | A tagged pattern names an invalid member or has no executable tagged-union layout. |
| `FSIM-ELAB-SVMATCH-009` | error | A structured pattern has incompatible aggregate metadata, member keys, member coverage, or executable layout. |
| `FSIM-ELAB-SVQUERY-001` | error | A bounded unpacked-container query has the wrong language or arity, an indirect expression, or no direct typed container object. |
| `FSIM-ELAB-SVQUERY-002` | error | A bounded unpacked-container query dimension is not the locally constant dimension `1`. |
| `FSIM-ELAB-SVQUERY-003` | error | A finite-bound query is applied to an associative array. |
| `FSIM-ELAB-SVQUERY-004` | error | A type-only SystemVerilog query is outside the bounded container-query subset. |
| `FSIM-ELAB-SVPATTERN-001` | error | A container assignment pattern has inconsistent metadata or uses keyed versus positional members with the wrong container kind. |
| `FSIM-ELAB-SVPATTERN-002` | error | A static pattern has the wrong element count or a dynamic pattern exceeds its container capacity. |
| `FSIM-ELAB-SVPATTERN-003` | error | An associative pattern key is not locally constant and known or duplicates another converted key. |
| `FSIM-ELAB-SVPATTERN-004` | error | A container assignment pattern mixes positional members with keyed/default members or uses `default` outside the bounded static-array subset. |
| `FSIM-ELAB-SVPATTERN-005` | error | A keyed/default static-array assignment pattern is missing its one required default member or contains duplicate defaults. |
| `FSIM-ELAB-SVPATTERN-006` | error | A static-array assignment-pattern index key is not a locally constant known integral value. |
| `FSIM-ELAB-SVPATTERN-007` | error | A static-array assignment-pattern index key is duplicate after signed conversion or outside the declared range. |
| `FSIM-ELAB-SVSLICE-001` | error | An unpacked slice is not a direct colon selection of a one-dimensional static array. |
| `FSIM-ELAB-SVSLICE-002` | error | A static-array slice bound is not a locally constant known integral value. |
| `FSIM-ELAB-SVSLICE-003` | error | A static-array slice reverses its declared direction or selects an out-of-range index. |
| `FSIM-ELAB-SVSLICE-004` | error | A static-array slice assignment source or destination is not a supported whole fixed array or direct fixed-array slice. |
| `FSIM-ELAB-SVSLICE-005` | error | Static-array slice assignment source and destination element counts differ. |
| `FSIM-ELAB-SVSLICE-006` | error | Static-array slice assignment source and destination element width, signedness, or state domains differ. |
| `FSIM-ELAB-SVREDUCE-001` | error | A container reduction is outside SystemVerilog-2017 or lacks a direct supported unpacked-container receiver. |
| `FSIM-ELAB-SVREDUCE-002` | error | A container reduction method has an argument or more than one retained `with` transformation. |
| `FSIM-ELAB-SVREDUCE-003` | error | A result-producing container reduction is used as a standalone statement. |
| `FSIM-ELAB-SVREDUCE-004` | error | A reduction `with` transformation uses unsupported arithmetic, calls, side effects, nonconstant operands, more than one conditional, or more than 64 graph nodes. |
| `FSIM-ELAB-SVREDUCE-005` | error | A reduction `with` transformation contains an invalid iterator/index reference or mixes element and index comparison profiles. |
| `FSIM-ELAB-SVREDUCE-006` | error | A reduction `with` transformation has a non-element root or uses an associative or otherwise excluded receiver profile. |
| `FSIM-ELAB-SVREDUCE-007` | error | A named reduction transformation iterator is malformed or collides with a visible object. |
| `FSIM-ELAB-SVORDER-001` | error | Container ordering lacks a direct writable supported SystemVerilog unpacked-container receiver. |
| `FSIM-ELAB-SVORDER-002` | error | A container ordering method has an invalid retained receiver, optional iterator, or key-expression shape. |
| `FSIM-ELAB-SVORDER-003` | error | A container ordering method is applied to an associative array. |
| `FSIM-ELAB-SVORDER-004` | error | A void container ordering method is used as an expression result. |
| `FSIM-ELAB-SVORDER-006` | error | A `sort` or `rsort` key uses unsupported arithmetic, calls, side effects, nonconstant operands, more than one conditional, or more than 64 graph nodes. |
| `FSIM-ELAB-SVORDER-007` | error | A `sort` or `rsort` key contains an invalid iterator/index reference or mixes element and index comparison profiles. |
| `FSIM-ELAB-SVORDER-008` | error | A `sort` or `rsort` key has a malformed or colliding named iterator, a non-element root, or an otherwise excluded receiver profile. |
| `FSIM-ELAB-SVLOCATOR-001` | error | A container locator lacks a direct supported nonassociative SystemVerilog unpacked-container receiver. |
| `FSIM-ELAB-SVLOCATOR-002` | error | A bounded container locator method has one or more arguments. |
| `FSIM-ELAB-SVLOCATOR-003` | error | A container locator result target is not a compatible queue. |
| `FSIM-ELAB-SVLOCATOR-004` | error | A container locator result is used outside a whole-queue assignment. |
| `FSIM-ELAB-SVLOCATOR-005` | error | A result-producing container locator is used as a standalone statement. |
| `FSIM-ELAB-SVLOCATOR-006` | error | An extrema/uniqueness locator transformation uses unsupported arithmetic, calls, side effects, nonconstant operands, more than one conditional, or more than 64 graph nodes. |
| `FSIM-ELAB-SVLOCATOR-007` | error | An extrema/uniqueness locator transformation contains an invalid iterator/index reference or mixes element and index comparison profiles. |
| `FSIM-ELAB-SVLOCATOR-008` | error | An extrema/uniqueness locator transformation has a malformed or colliding named iterator, a non-element root, or an associative or otherwise excluded receiver profile. |
| `FSIM-ELAB-SVFIND-001` | error | A predicate container locator lacks a direct supported nonassociative SystemVerilog unpacked-container receiver. |
| `FSIM-ELAB-SVFIND-002` | error | A predicate container locator does not retain exactly one `with`-clause predicate. |
| `FSIM-ELAB-SVFIND-003` | error | A predicate container locator result target is not a compatible value or signed 32-bit index queue. |
| `FSIM-ELAB-SVFIND-004` | error | A predicate container locator uses an unsupported, nonconstant, mistyped, or overlarge predicate expression. |
| `FSIM-ELAB-SVFIND-005` | error | A result-producing predicate container locator is used as a standalone statement. |
| `FSIM-ELAB-SVFIND-006` | error | A predicate container locator result is used outside a whole-queue assignment. |
| `FSIM-ELAB-SVFIND-007` | error | A named predicate-container iterator is malformed or collides with a visible object. |
| `FSIM-ELAB-SVFIND-008` | error | A predicate-container iterator has an unknown or unsupported index reference, selection, call, or mixed element/index comparison profile. |
| `FSIM-ELAB-SVMEMORY-002` | error | A memory-file task file name is not a bounded string expression. |
| `FSIM-ELAB-SVMEMORY-003` | error | A memory-file task object is not a direct bounded static unpacked-array object. |
| `FSIM-ELAB-SVMEMORY-004` | error | A memory-file task start or finish argument is not a 32-bit integral expression. |
| `FSIM-ELAB-SVPORT-001` | error | A SystemVerilog container port lacks a supported one-dimensional integral element or associative-index type. |
| `FSIM-ELAB-SVPORT-002` | error | A bounded-queue maximum is unknown or negative, or a static-array range does not fit the per-container owning-storage budget. |
| `FSIM-ELAB-SVPORT-003` | error | A container input port attempts to use an unsupported default connection value. |
| `FSIM-ELAB-SVPORT-004` | error | A SystemVerilog container port attempts to cross a language boundary. |
| `FSIM-ELAB-SVPORT-005` | error | A container-port actual is an expression, selection, or slice rather than a direct whole-container object. |
| `FSIM-ELAB-SVPORT-006` | error | A container-port actual is unknown, unresolved, or omitted from a required input connection. |
| `FSIM-ELAB-SVPORT-007` | error | A container formal and actual differ in kind, element/index profile, queue bound, or exact static range. |
| `FSIM-ELAB-SVPORT-008` | error | Independent output or inout module-port paths drive the same container object. |
| `FSIM-ELAB-SVPORT-009` | error | An input container port would be modified locally or through a descendant output/inout connection. |
| `FSIM-ELAB-SVPORT-010` | error | A mutable string module port is unconnected, crosses a language boundary, or does not use a direct same-language string object actual. |
| `FSIM-ELAB-SVPORT-011` | error | A mutable string input port is written directly or through a descendant output/inout port. |
| `FSIM-ELAB-SVPORT-012` | error | Sibling output/inout mutable string ports drive the same object. |
| `FSIM-ELAB-SVPROCASSIGN-001` | error | A procedural continuous assignment cannot allocate its activation state or does not target a visible process-external packed variable. |
| `FSIM-ELAB-SVPROCASSIGN-002` | error | A procedural continuous assignment cannot allocate its generated reactive driver process. |
| `FSIM-ELAB-SVSCALAR-001` | error | A contextual SystemVerilog scalar literal cannot be converted to its required scalar kind. |
| `FSIM-ELAB-SVSCALAR-002` | error | A runtime real/time/chandle expression uses an operator outside the executable arithmetic/comparison subset. |
| `FSIM-ELAB-SVSTRING-001` | error | A SystemVerilog string parameter/localparam default is not a supported immutable constant-string expression. |
| `FSIM-ELAB-SVSTRING-002` | error | A SystemVerilog parameter actual crosses the bounded integral/string type boundary or is not a supported constant string. |
| `FSIM-ELAB-SVSTRING-003` | error | A bounded output/report message position contains a string expression that is not constant after specialization. |
| `FSIM-ELAB-SVSTRING-005` | error | A runtime string variable is duplicated in one automatic scope. |
| `FSIM-ELAB-SVSTRING-006` | error | A runtime string literal has no retained decoded byte value. |
| `FSIM-ELAB-SVSTRING-007` | error | A runtime string literal, initializer, object, or concatenation exceeds the 4,096-byte v1 limit. |
| `FSIM-ELAB-SVSTRING-008` | error | A runtime string expression references an unknown string object. |
| `FSIM-ELAB-SVSTRING-009` | error | A runtime string concatenation has no operands. |
| `FSIM-ELAB-SVSTRING-010` | error | A runtime string concatenation or value context contains a non-string operand. |
| `FSIM-ELAB-SVSTRING-011` | error | A runtime string function call has an unknown or incompatible callable profile. |
| `FSIM-ELAB-SVSTRING-012` | error | Runtime string equality or inequality mixes string and non-string operands. |
| `FSIM-ELAB-SVSTRING-013` | error | A runtime string assignment is nonblocking or has a timing/event control. |
| `FSIM-ELAB-SVSTRING-014` | error | A whole-string assignment has a non-string right-hand side. |
| `FSIM-ELAB-SVSTRING-015` | error | A runtime string assignment target is neither a whole object nor one byte index. |
| `FSIM-ELAB-SVSTRING-016` | error | A retained module variable is not in the supported runtime string domain. |
| `FSIM-ELAB-SVSTRING-017` | error | A module string initializer is not a supported bounded static string expression. |
| `FSIM-ELAB-SVSTRING-018` | error | A runtime string method has an unsupported receiver, argument, or result context. |
| `FSIM-ELAB-SVSTRING-019` | error | `$swrite`, `$sformat`, or `$sformatf` has a nonliteral, malformed, oversized, under-supplied, or unsupported bounded format/value list. |
| `FSIM-ELAB-SVSTRING-020` | error | `$swrite` or `$sformat` does not name a direct writable string target with positional arguments. |
| `FSIM-ELAB-SVTYPENAME-001` | error | `$typename` does not have exactly one statically typed SystemVerilog expression or type, or its result exceeds bounded string storage. |
| `FSIM-ELAB-SVTYPEPARAM-001` | error | A required SystemVerilog type parameter or local type parameter has no data-type actual/default. |
| `FSIM-ELAB-SVTYPEPARAM-002` | error | A SystemVerilog value parameter received a data-type actual. |
| `FSIM-ELAB-SVTYPEPARAM-003` | error | A SystemVerilog type-parameter actual/default is invisible or outside the bounded packed integral subset. |
| `FSIM-ELAB-SVTYPEPARAM-004` | error | A SystemVerilog type parameter was associated across a mixed-language boundary instead of through a same-language wrapper. |
| `FSIM-ELAB-SVSTRUCT-001` | error | A packed-struct member range or total layout cannot be specialized into a supported width. |
| `FSIM-ELAB-SVSTRUCT-002` | error | A packed-aggregate member read/write has no executable normalized layout. |
| `FSIM-ELAB-SVUNION-001` | error | Packed/tagged-union layout, construction context, discriminator, or selected-write timing is invalid. |
| `FSIM-ELAB-SVAGG-001` | error | An aggregate assignment pattern has inconsistent contextual layout or association metadata. |
| `FSIM-ELAB-SVAGG-002` | error | An aggregate assignment-pattern key is not a direct member name or names an unknown member. |
| `FSIM-ELAB-SVAGG-003` | error | An aggregate assignment pattern duplicates, omits, or supplies too many members or defaults. |
| `FSIM-ELAB-SVAGG-004` | error | An aggregate assignment-pattern member value has the wrong width. |
| `FSIM-ELAB-SVAGG-005` | error | A two-state aggregate assignment-pattern member receives a four-state value without conversion. |
| `FSIM-ELAB-SVAGG-006` | error | A packed-union assignment pattern does not select exactly one member or uses `default`. |
| `FSIM-ELAB-SVAGG-007` | error | A packed aggregate member initializer is not a valid contextual constant. |
| `FSIM-ELAB-SVCAST-001` | error | Malformed cast HIR does not contain exactly one operand. |
| `FSIM-ELAB-SVCAST-002` | error | A SystemVerilog cast names a type that is not visible. |
| `FSIM-ELAB-SVCAST-003` | error | A SystemVerilog cast type has zero width or exceeds the host-addressable executable-width domain. |
| `FSIM-ELAB-SVCAST-004` | error | Reserved non-emitting identity formerly used for four-state-to-two-state cast rejection. |
| `FSIM-ELAB-SVMDARRAY-001` | error | A multidimensional static-array access does not supply exactly one index per declared unpacked dimension. |
| `FSIM-ELAB-SVMDARRAY-002` | error | A runtime multidimensional static-array index cannot lower to a signed 32-bit integral value. |
| `FSIM-ELAB-SVMDARRAY-003` | error | A multidimensional static-array index is outside its declared range or cannot be flattened within the bounded capacity. |
| `FSIM-ELAB-SVREPL-001` | error | A replication concatenation has a nonconstant/nonpositive count, no statically sized operands, or an overflowing expanded width. |
| `FSIM-ELAB-SVSPEC-001` | error | A specify parameter, condition operand, path delay, or timing-check value is not locally static after specialization. |
| `FSIM-ELAB-SVSPEC-002` | error | A specify terminal does not name a direct signal or port in its owning module instance. |
| `FSIM-ELAB-SVSPEC-003` | error | A specify path terminal has a direction incompatible with its source or destination role. |
| `FSIM-ELAB-SVSPEC-004` | error | A specify path terminal does not have a static nonzero packed width. |
| `FSIM-ELAB-SVSPEC-005` | error | A parallel module path does not pair equal-width source and destination terminals. |
| `FSIM-ELAB-SVSPEC-006` | error | A restored or native module path has a delay count other than 1, 2, 3, 6, or 12. |
| `FSIM-ELAB-SVSPEC-007` | error | A module-path delay is negative, nonstatic, overflowing, or not normalized to project ticks. |
| `FSIM-ELAB-SVSPEC-008` | error | A specify condition or destination-data expression is outside the bounded executable integral expression model. |
| `FSIM-ELAB-SVSPEC-009` | error | A destination-data expression is neither scalar nor width-compatible with every destination. |
| `FSIM-ELAB-SVSPEC-010` | error | An edge-sensitive module path has a nonscalar source terminal. |
| `FSIM-ELAB-SVSPEC-011` | error | A `PATHPULSE` limit is not a representable normalized project time. |
| `FSIM-ELAB-SVSPEC-012` | error | A `PATHPULSE` rejection limit exceeds its error limit. |
| `FSIM-ELAB-SVSPEC-013` | error | A terminal-specific `PATHPULSE` selector does not name signals in the owning module. |
| `FSIM-ELAB-SVSPEC-014` | error | A timing-check event terminal is not scalar. |
| `FSIM-ELAB-SVSPEC-015` | error | A timing-check notifier is not a scalar signal. |
| `FSIM-ELAB-SVSPEC-016` | error | A timing-check limit violates its normalization, range, sign, compound-sum, or ordering requirement. |
| `FSIM-ELAB-SVSPEC-017` | error | A delayed timing-check reference or data terminal is not scalar. |
| `FSIM-ELAB-SVSPEC-018` | error | A timing-check event-based or remain-active flag is not a static two-state value. |
| `FSIM-ELAB-SVSPEC-019` | error | A controlled timing-check event lacks its required edge or contains an invalid scalar transition descriptor. |
| `FSIM-ELAB-GENERIC-001` | error | A VHDL generic actual is unknown, missing, excessive, or cannot target the selected SystemC factory. |
| `FSIM-ELAB-GENERIC-002` | error | A VHDL generic receives more than one actual. |
| `FSIM-ELAB-GENERIC-003` | error | A positional VHDL generic actual follows a named actual. |
| `FSIM-ELAB-GENERIC-004` | error | A VHDL generic actual constant expression cannot be evaluated. |
| `FSIM-ELAB-GENERIC-005` | error | A VHDL generic default constant expression cannot be evaluated. |
| `FSIM-ELAB-GENERIC-006` | error | A generic-dependent packed range cannot be evaluated. |
| `FSIM-ELAB-GENERIC-007` | error | A generic-dependent packed range width overflows the supported range. |
| `FSIM-ELAB-GENERIC-008` | error | A generic value violates its bounded scalar subtype constraint. |
| `FSIM-ELAB-GENERIC-009` | error | An architecture signal conflicts with an entity generic. |
| `FSIM-ELAB-GENERIC-010` | error | A bounded subtype-typed VHDL generic resolves outside the supported scalar, physical-time, packed, or statically constrained composite value set. |
| `FSIM-ELAB-GENTYPE-001` | error | A required VHDL interface type generic has no associated subtype indication. |
| `FSIM-ELAB-GENTYPE-002` | error | A VHDL interface type generic actual is not syntactically a subtype indication. |
| `FSIM-ELAB-GENTYPE-003` | error | A VHDL interface type generic actual names a type that is not visible at the association. |
| `FSIM-ELAB-GENTYPE-004` | error | A VHDL interface type generic is given an actual through a non-VHDL association boundary. |
| `FSIM-ELAB-GENTYPE-005` | error | A VHDL value generic is given an unambiguous subtype-indication actual. |
| `FSIM-ELAB-PARAM-001` | error | A parameter override has an unknown/local target, is missing or excessive, or is applied to a SystemC factory. |
| `FSIM-ELAB-PARAM-002` | error | A parameter override is duplicated during elaboration. |
| `FSIM-ELAB-PARAM-003` | error | Named and positional parameter overrides are mixed during elaboration. |
| `FSIM-ELAB-PARAM-004` | error | A parameter override constant expression cannot be evaluated. |
| `FSIM-ELAB-PARAM-005` | error | A parameter default constant expression cannot be evaluated. |
| `FSIM-ELAB-PARAM-006` | error | A parameterized packed range cannot be evaluated. |
| `FSIM-ELAB-PARAM-007` | error | A parameterized packed range width overflows the supported range. |
| `FSIM-ELAB-PARAM-009` | error | A case-insensitive VHDL named generic actual ambiguously matches multiple case-sensitive parameters on a foreign Verilog/SystemVerilog target. |
| `FSIM-ELAB-SC-PARAM-001` | error | A SystemC construction actual is unknown, missing, or excessive. |
| `FSIM-ELAB-SC-PARAM-002` | error | A SystemC construction parameter receives more than one actual. |
| `FSIM-ELAB-SC-PARAM-003` | error | Construction actual association ordering is illegal for the HDL parent language. |
| `FSIM-ELAB-SC-PARAM-004` | error | A SystemC construction actual constant expression cannot be evaluated in the parent specialization. |
| `FSIM-ELAB-SC-PARAM-005` | error | A construction value violates the factory parameter's integer/natural/positive/Boolean/bit subtype. |
| `FSIM-ELAB-SC-PARAM-006` | error | A VHDL named actual ambiguously matches case-distinct SystemC factory parameters. |
| `FSIM-ELAB-SC-PARAM-007` | error | The selected SystemC factory schema cannot be inspected. |
| `FSIM-ELAB-SC-PARAM-008` | error | The schema-validated SystemC instance cannot be constructed or has inconsistent provider identity. |
| `FSIM-ELAB-TYPE-001` | error | A signal type cannot be represented by the packed runtime. |

### Bindings and mixed-language boundaries

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-ELAB-BIND-0001` | error | A manifest binding target is not language-qualified. |
| `FSIM-ELAB-BIND-0002` | error | A manifest binding uses an unsupported language. |
| `FSIM-ELAB-BIND-0003` | error | A manifest binding target unit was not found. |
| `FSIM-ELAB-BIND-0004` | error | A manifest binding resolver is neither `std_logic` nor `sv_wire`. |
| `FSIM-ELAB-BIND-010` | error | More than one binding names the same instance path. |
| `FSIM-ELAB-BIND-011` | error | A binding path was not found in the elaborated hierarchy. |
| `FSIM-ELAB-BIND-012` | error | An unqualified instance spelling has no VHDL, Verilog/SystemVerilog, or SystemC candidate in its complete effective logical-library scope. |
| `FSIM-ELAB-BIND-013` | error | An elaboration binding target is malformed. |
| `FSIM-ELAB-BIND-014` | error | A SystemC binding reached HDL target selection without a matching preconstructed typed factory instance. |
| `FSIM-ELAB-BIND-015` | error | An explicit binding target was not found. |
| `FSIM-ELAB-BIND-016` | error | An explicit VHDL binding does not name an architecture. |
| `FSIM-ELAB-BIND-017` | error | An unqualified instance spelling has multiple canonical candidates across its complete effective logical-library scope. |
| `FSIM-ELAB-BIND-018` | error | A logical library exposes the same public SystemC factory name more than once. |
| `FSIM-ELAB-BIND-019` | error | A boundary port or actual uses an unsupported value domain. |
| `FSIM-ELAB-BIND-020` | error | Boundary port and actual widths differ. |
| `FSIM-ELAB-BIND-021` | error | Boundary port and actual signedness differ. |
| `FSIM-ELAB-BIND-022` | error | A boundary connection would implicitly lose state when converted to a two-state destination. |
| `FSIM-ELAB-BIND-023` | error | Conflicting resolvers were assigned to one boundary net. |
| `FSIM-ELAB-BIND-024` | error | Multiple boundary drivers lack the required explicit resolver. |
| `FSIM-ELAB-BIND-025` | error | A named port is unknown or there are too many positional connections. |
| `FSIM-ELAB-BIND-026` | error | An instance port is connected more than once. |
| `FSIM-ELAB-BIND-027` | error | A boundary actual is not a whole signal, an input is illegally open, or a required VHDL direct-entity input is unassociated. |
| `FSIM-ELAB-BIND-028` | error | A boundary actual names an unknown parent signal. |
| `FSIM-ELAB-BIND-030` | error | A cross-language `inout` lacks an explicit resolver. |
| `FSIM-ELAB-BIND-031` | error | Same-language VHDL packed-array boundary bounds or direction differ. |
| `FSIM-ELAB-BIND-032` | error | More than one preconstructed SystemC description names the same instance path. |
| `FSIM-ELAB-BIND-033` | error | A preconstructed SystemC instance was not reached from the selected top. |
| `FSIM-ELAB-BIND-038` | error | An HDL-to-SystemC binding has no matching preconstructed factory instance. |
| `FSIM-ELAB-BIND-039` | error | A preconstructed SystemC instance target differs from its manifest binding. |
| `FSIM-ELAB-BIND-043` | error | A SystemC process sensitivity references an unknown registered object. |
| `FSIM-ELAB-BIND-044` | error | A SystemC process registered an invalid sensitivity edge. |
| `FSIM-ELAB-BIND-045` | error | A SystemC edge sensitivity references a non-scalar object. |
| `FSIM-ELAB-BIND-046` | error | A SystemC port/channel binding references an unknown internal signal or aliases one internal signal to conflicting parent signals. |
| `FSIM-ELAB-BIND-047` | error | A constructed native SystemC child has an inconsistent parent handle or direct-child path. |
| `FSIM-ELAB-BIND-048` | error | A typed SystemC export is unbound, cyclic, references an unknown object, or conflicts with another hierarchy alias. |
| `FSIM-ELAB-BIND-049` | error | A packed aggregate crosses a language boundary without a same-language scalar/vector wrapper. |
| `FSIM-ELAB-BIND-050` | error | An elaboration binding names a resolver other than `std_logic` or `sv_wire`. |
| `FSIM-ELAB-BIND-051` | error | An integer hierarchy boundary cannot guarantee a range-safe same-language alias or cross-language 32-bit signed conversion in the port's data-flow direction. |
| `FSIM-ELAB-BIND-052` | error | A VHDL enumeration crosses a language boundary without a same-language scalar/vector wrapper. |
| `FSIM-ELAB-BIND-053` | error | A same-language hierarchy boundary connects different nominal enumeration types. |
| `FSIM-ELAB-BIND-054` | error | Same-language VHDL enumeration subtype ranges cannot guarantee a range-safe alias in the port's data-flow direction. |
| `FSIM-ELAB-BIND-055` | error | A VHDL array crosses a language boundary without a same-language scalar/vector wrapper. |
| `FSIM-ELAB-BIND-056` | error | A same-language VHDL hierarchy boundary connects different nominal array types. |
| `FSIM-ELAB-BIND-057` | error | A same-language hierarchy boundary connects different nominal aggregate or record types. |
| `FSIM-ELAB-BIND-058` | error | A native SystemC child port is unbound after factory construction. |
| `FSIM-ELAB-BIND-059` | error | An unqualified child lookup needs one or more configured logical libraries which are unavailable. |
| `FSIM-ELAB-BIND-060` | error | A user-defined primitive instance supplies module parameter overrides. |
| `FSIM-ELAB-BIND-061` | error | A user-defined primitive instance uses named rather than positional terminal connections. |
| `FSIM-ELAB-BIND-062` | error | An ordinary module instance omits its required instance name. |
| `FSIM-ELAB-BIND-063` | error | An ordinary module instance uses syntax reserved for a UDP propagation delay. |
| `FSIM-ELAB-BIND-064` | error | A user-defined primitive instance-array terminal is neither scalar nor width-matched to the array. |
| `FSIM-ELAB-BIND-065` | error | An ordinary module instance uses syntax reserved for a UDP drive strength. |

## Time, runtime, trace, and design cache

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-TIME-0001` | error | The project resolution cannot be converted to a legal VCD timescale. |
| `FSIM-TIME-0002` | error | The configured run duration is invalid or not representable at project resolution. |
| `FSIM-TIME-0003` | error | An HDL delay is invalid or not representable at project resolution. |
| `FSIM-TIME-0004` | error | A declared SystemVerilog time precision is not representable at project resolution. |
| `FSIM-RUN-0001` | error | The SimIR interpreter failed during a CLI run. |
| `FSIM-RUN-0002` | error | Another exception terminated a CLI run. |
| `FSIM-UVM-CLI-001` | error | A recognized UVM command-line plusarg is malformed, exceeds a bound, or cannot be applied to the simulation-owned factory/config/resource state. |
| `FSIM-UVM-PHASE-001` | error | A UVM phase or domain handle is empty, stale, or owned by another simulation. |
| `FSIM-UVM-PHASE-002` | error | A UVM phase/domain identity, standard kind, registration, root participation, or graph edge is malformed or duplicated. |
| `FSIM-UVM-PHASE-003` | error | A UVM phase parent or dependency edge crosses domains, exceeds structural depth, or would make the graph cyclic or inconsistent. |
| `FSIM-UVM-PHASE-004` | error | A UVM phase graph exceeds its domain, node, edge, root, identity, traversal, or mutation resource ceiling. |
| `FSIM-UVM-PHASE-005` | error | A UVM phase domain references a root that is not live in the owning simulation. |
| `FSIM-UVM-PHASE-006` | error | A per-component UVM phase callback threw or otherwise failed; the phase contains the failure and continues deterministic traversal. |
| `FSIM-UVM-PHASE-007` | error | A UVM task-phase process tree, scheduled wait, synchronized execution group, completion, timeout, or jump request is invalid for its current state or domain. |
| `FSIM-UVM-PHASE-008` | error | UVM task-phase quiescence deadlocked or exceeded its ready-to-end re-entry, scheduler callback, zero-time stabilization, delta-cycle, or iteration ceiling. |
| `FSIM-UVM-COPY-001` | error | A UVM copier source, destination, or recursively traversed object handle is empty or stale. |
| `FSIM-UVM-COPY-002` | error | A branded UVM copier source or destination handle belongs to another simulation-owned object service. |
| `FSIM-UVM-COPY-003` | error | UVM copier source/destination types or automated field property shapes are incompatible. |
| `FSIM-UVM-COPY-004` | error | A UVM copier recursion, object, field, or recursive-creation resource ceiling was exceeded; destination state and new objects were rolled back. |
| `FSIM-UVM-COPY-005` | error | A UVM virtual creation or `do_copy` automation callback threw or returned incompatible state; destination state and new objects were rolled back. |
| `FSIM-UVM-PACK-001` | error | A UVM packed payload has an invalid header, schema, metadata/endian policy, item tag, width, length, truncation, or trailing data. |
| `FSIM-UVM-PACK-002` | error | UVM packing or unpacking exceeded its configured recursion-depth, item, bit, text, or payload-byte ceiling. |
| `FSIM-UVM-POLICY-001` | error | A UVM printer, comparer, copier, packer-endian, or automated field policy has an unknown kind, conflicting recursion or abstraction flags, a malformed separator or indentation token, an invalid table width, or an out-of-range retained-mismatch limit. |
| `FSIM-UVM-OBJ-001` | error | A UVM objection source, phase, root, or component handle is empty, stale, or owned by another simulation. |
| `FSIM-UVM-OBJ-002` | error | A UVM objection operation has an invalid count, phase state, root association, or local drop underflow. |
| `FSIM-UVM-OBJ-003` | error | UVM objection source, description, entry, count, propagation, callback, trace, mutation, drain, aggregate delay, or re-entry state exceeds its configured ceiling. |
| `FSIM-UVM-OBJ-004` | error | A UVM raised, dropped, all-dropped, or ready-to-end callback threw; the failure is contained after the transactional count update and remaining work continues in deterministic order. |
| `FSIM-UVM-TLM1-001` | error | A UVM TLM1 endpoint, owning component, or root is empty, stale, or owned by another simulation. |
| `FSIM-UVM-TLM1-002` | error | A UVM TLM1 endpoint name, cardinality, duplicate connection, required binding, or terminal resolution is malformed. |
| `FSIM-UVM-TLM1-003` | error | A UVM TLM1 connection has incompatible endpoint kinds, interface/profile, direction, root identity, or would introduce a cycle. |
| `FSIM-UVM-TLM1-004` | error | A UVM TLM1 endpoint, connection, component fanout, graph depth, traversal work, profile storage, or mutation resource ceiling was exceeded. |
| `FSIM-UVM-TLM1-005` | error | A UVM TLM1 operation is unsupported by its interface, uses an invalid binding or phase state, has a mismatched nominal payload type, or addresses an unavailable FIFO or transport implementation. |
| `FSIM-UVM-TLM1-006` | error | UVM TLM1 FIFO capacity, queued payload, payload storage, pending operation, execution count, or sequence state exceeds its configured ceiling. |
| `FSIM-UVM-TLM1-007` | error | A UVM TLM1 analysis publication, subscriber, FIFO, implementation, or callback is invalid; subscriber failures are contained and remaining snapshot targets continue in resolution order. |
| `FSIM-UVM-TLM1-008` | error | UVM TLM1 analysis publication exceeds its callback fanout, recursion depth, retained failure, publication count, or delivery sequence ceiling. |
| `FSIM-UVM-TLM2-001` | error | A UVM TLM2 socket or transaction handle is empty, stale, or owned by another simulation. |
| `FSIM-UVM-TLM2-002` | error | A UVM TLM2 socket registration, connection, binding, kind, protocol, nominal profile, root, cardinality, or passthrough topology is invalid. |
| `FSIM-UVM-TLM2-003` | error | A UVM TLM2 generic payload, byte-enable pattern, streaming width, extension set, response, direct-memory descriptor, or ownership association is malformed. |
| `FSIM-UVM-TLM2-004` | error | A UVM TLM2 transport callback, protocol phase, debug result, transaction transition, or forward/backward operation is invalid or failed. |
| `FSIM-UVM-TLM2-005` | error | UVM TLM2 payload, byte-enable, streaming-width, extension, hop, callback, socket, transaction, or outstanding-work state exceeds its configured ceiling. |
| `FSIM-UVM-SEQ-001` | error | A UVM sequence-item, sequence, sequencer, component-role, or handshake handle or its backing object, component, root, parent, or owner is empty or stale. |
| `FSIM-UVM-SEQ-002` | error | A UVM sequence-item, sequence, sequencer, component-role, or handshake handle belongs to another simulation-owned sequence service. |
| `FSIM-UVM-SEQ-003` | error | A UVM sequence-item, sequence, or sequencer nominal type or request/response profile is empty or incompatible. |
| `FSIM-UVM-SEQ-004` | error | A UVM sequence or item name, parent/child relationship, sequencer association, root association, or sibling identity is malformed or inconsistent. |
| `FSIM-UVM-SEQ-005` | error | A UVM sequence object, sequencer component, or component role is registered twice; a role phase is out of order; or release was requested while live owned state remains. |
| `FSIM-UVM-SEQ-006` | error | UVM sequence-item, sequence, sequencer, component-role, virtual domain/step/restart/event, registration, dispatch, analysis, mutation, hierarchy, identity, profile, or ownership state exceeds its configured resource ceiling. |
| `FSIM-UVM-SEQ-007` | error | A UVM sequence lifecycle callback, automatic objection, or owned phase-process completion failed; the failure is contained in the execution result and the sequence stops deterministically. |
| `FSIM-UVM-SEQ-008` | error | A UVM sequencer arbitration mode, request priority, relevance callback, user-selection callback, callback re-entry, or selected candidate is invalid. |
| `FSIM-UVM-SEQ-009` | error | A UVM sequence response queue depth, overflow policy, shrink request, or configured overflow-error action is invalid. |
| `FSIM-UVM-SEQ-010` | error | A UVM sequence lock/grab request, nested owner, unlock/ungrab operation, cancellation, or sequencer association is invalid. |
| `FSIM-UVM-SEQ-011` | error | A UVM driver/sequencer pull or push handshake, transaction state transition, phase/process binding, response association, cancellation reason, or live reconfiguration is invalid. |
| `FSIM-UVM-SEQ-012` | error | A UVM driver, monitor, active/passive agent, subscriber, or scoreboard has an invalid hierarchy, configuration, sequencer binding, analysis connection, phase/process context, objection transition, callback, or role-specific operation. |
| `FSIM-UVM-SEQ-013` | error | A UVM virtual sequencer domain or virtual-sequence coordination has an invalid typed child binding, root, priority, lock/grab state, phase/process context, reset/restart request, duplicate step, or lifecycle transition. |
| `FSIM-UVM-DEBUG-001` | error | UVM phase, objection, process, connection, FIFO, socket, payload, drain, or transaction state advanced, became stale, or was torn down while a public debug snapshot was captured. |
| `FSIM-UVM-DEBUG-002` | error | A UVM debug snapshot or formatted view exceeds its configured record, payload-byte, or output-byte ceiling. |
| `FSIM-UVM-ACTIVITY-001` | error | A UVM activity observer is invalid or threw while receiving an immutable event snapshot; observer failures are contained and remaining callbacks continue in registration order. |
| `FSIM-UVM-ACTIVITY-002` | error | UVM activity event retention, observer count, callback fanout, re-entry depth, text storage, sequence state, or configured resource limits were exceeded. |
| `FSIM-UVM-CALLBACK-001` | error | A type-wide or instance UVM callback registration, handle, target type, ordering, dispatch mask, immutable routing identity, or lifecycle operation is invalid; callback exceptions are contained and their invocation mutations are rolled back. |
| `FSIM-UVM-CALLBACK-002` | error | UVM callback registration, dispatch fanout, re-entry, failure retention, attribute/text storage, identity, order, or mutation state exceeds its configured resource ceiling. |
| `FSIM-UVM-TR-001` | error | A UVM transaction begin/end, object/root, parent/link relation, attribute, terminal state, handle, release, or callback-time lifecycle operation is invalid. |
| `FSIM-UVM-TR-002` | error | UVM transaction retention, active count, attribute/link/trace storage, text, identity, order, or mutation state exceeds its configured resource ceiling. |
| `FSIM-UVM-CMD-001` | error | A recognized UVM command-line setting, trace-switch form, field, numeric value, delimiter, duplicate policy, or exact/prefix/value query is malformed or invalid. |
| `FSIM-UVM-CMD-002` | error | UVM command-line argv, setting, query, query-result, text, or downstream config/resource trace, inventory, or entry capacity exceeded its configured ceiling. |
| `FSIM-UVM-RUN-001` | error | A UVM `run_test` selection, root, test creation, repeated-run lifecycle, or re-entry operation is missing, stale, invalid, or incompatible. |
| `FSIM-UVM-RUN-002` | error | UVM `run_test` count, name, topology, message, or global-timeout work exceeded its configured ceiling. |
| `FSIM-UVM-RUN-003` | error | A UVM test terminated through a fatal result or a contained execution exception. |
| `FSIM-UVM-SYNC-001` | error | A UVM event, barrier, pool, queue, heartbeat, waiter, callback, process owner, object payload, threshold, mode, key, index, or lifecycle operation is empty, stale, duplicated, out of range, or invalid for its current state. |
| `FSIM-UVM-SYNC-002` | error | UVM event/barrier/waiter/callback/pool/queue/heartbeat/spell-check text, identity, entry, participant, failure, work, or mutation retention exceeded its configured resource ceiling. |
| `FSIM-UVM-REG-001` | error | A UVM register-model block, map, register, field, or memory handle, root, hierarchy, name, ownership relation, width, offset, dimension, field layout, build state, or lock operation is invalid. |
| `FSIM-UVM-REG-002` | error | UVM register-model block, map, register, field, memory, declaration, hierarchy depth, name, width, aggregate register-value bits, dimension, extent, identity, order, or mutation state exceeds its configured resource ceiling. |
| `FSIM-UVM-REG-003` | error | A UVM register or memory configuration, value width, X/Z value, access right, prediction kind, reset kind, index, build/lock state, or value operation is invalid; failed operations leave desired, mirrored, write-once, and sparse memory state unchanged. |
| `FSIM-UVM-REG-004` | error | UVM register reset kinds, reset-name/value storage, materialized memory words/bits, operation attempts, or runtime value mutations exceed their configured resource ceiling. |
| `FSIM-UVM-REG-005` | error | A UVM register-map bus, address unit, endian mode, register/memory/submap membership, hierarchy, rights, byte enable, burst range, alignment, overlap, address, or lookup operation is invalid; failed construction, lock, and burst operations leave map and value state unchanged. |
| `FSIM-UVM-REG-006` | error | UVM register-map memberships, per-map registrations, submap depth, bus width, physical beats, aggregate physical byte enables, burst words/value bits/storage/work, lookup work, or mapped operation resources exceed their configured ceiling. |
| `FSIM-UVM-REG-007` | error | A UVM register adapter, predictor, frontdoor service, root, sequencer, sequence, TLM analysis implementation, target, selected-map right, callback, bus response, operation state, completion, cancellation, or explicit prediction is invalid; callback and response failures are contained without partial mirror prediction. |
| `FSIM-UVM-REG-008` | error | UVM register adapters, predictors, frontdoor operations, pending operations, retained bus items, value bits, aggregate byte enables, predictor observations, identities, registration order, or mutation state exceed their configured resource ceiling. |
| `FSIM-UVM-REG-009` | error | A UVM user frontdoor, register/field/memory target, root, callback, HDL abstraction, VPI/VHPI path, slice, concatenation, resolution, read/deposit/force/release operation, value width, or relocated transport is invalid; callback failures are contained and failed multi-slice writes restore already-applied deposits where possible. |
| `FSIM-UVM-REG-010` | error | UVM user frontdoors, HDL paths, aggregate slices/path bytes, backdoor operations/value bits, identities, registration order, or mutation state exceed their configured resource ceiling. |
| `FSIM-UVM-REG-011` | error | Invalid UVM standard register-sequence root, map, reset, exclusion, transport, target, rights, result, or comparison state. |
| `FSIM-UVM-REG-012` | error | UVM standard register sequences, operations, failures/failure bytes, exclusions/exclusion bytes, identities, execution order, or mutation state exceed their configured resource ceiling. |
| `FSIM-UVM-REG-013` | error | Invalid UVM register callback/coverage scope, target, root, map rights, phase mask, callback, mutation, status, value, index, or access state. |
| `FSIM-UVM-REG-014` | error | UVM register callbacks, per-access dispatches, invocations/failures, coverage models/samples/bins/bin bytes, identities, registration order, or mutation state exceed their configured resource ceiling. |
| `FSIM-UVM-FOREIGN-001` | error | A DPI/VPI UVM snapshot layout, record index, simulation identity, generation, or foreign callback request is invalid, stale, or belongs to another simulation. |
| `FSIM-UVM-FOREIGN-002` | error | A DPI/VPI UVM snapshot, record, text, payload, caller buffer, callback, or retained-generation resource ceiling was exceeded. |
| `FSIM-UVM-STATE-001` | error | A versioned UVM artifact has an invalid schema, ABI, provenance, phase graph, objection/drain, TLM connection/FIFO/transaction record, or contains a nonportable process object. |
| `FSIM-UVM-STATE-002` | error | Portable UVM checkpoint construction or capture exceeded a configured record, root, identity, text, payload, callback-summary, or scheduler-state resource ceiling. |
| `FSIM-UVM-VERSION-001` | error | Governed UVM release selection is invalid, applies to a non-SystemVerilog source set, mixes UVM 1.2 with UVM 2020-3.1 sources or objects, or disagrees with checked artifact provenance. |
| `FSIM-UVM-VERSION-002` | error | The selected or retained governed UVM release disagrees with the parsed or portable `uvm_pkg` API surface. |
| `FSIM-RUN-ASSERT-0001` | assertion severity | A false HDL assertion stopped a CLI simulation; the diagnostic retains its source location and message. |
| `FSIM-HDL-REPORT` | report severity | A VHDL report or SystemVerilog severity task was emitted through a command or Tcl output stream. |
| `FSIM-RUN-DELTA-0001` | error | Simulation exceeded `max_deltas`; the message includes pending processes and recent signals. |
| `FSIM-VCD-0001` | error | The trace output directory could not be created. |
| `FSIM-VCD-0002` | error | The VCD trace file could not be opened. |
| `FSIM-VCD-0003` | error | VCD declaration, immutable selection ownership, append-only late-snapshot/value emission, timestamp scaling, flushing, staged publication, or terminal lifecycle completion failed. The first terminal failure is retained and reported once; an incomplete trace is never reported as cleanly complete. |
| `FSIM-TRACE-0001` | error | Trace-format selection conflicts with the output extension or the requested FST timescale cannot be represented exactly. |
| `FSIM-TRACE-0002` | error | A trace destination, staging file, or exclusive output lock is unsafe or unavailable. |
| `FSIM-TRACE-0003` | error | Trace declaration, canonical multiple-root hierarchy, language/library/owner provenance, structural alias identity, immutable selective-trace ownership, debugger/Tcl add/remove/status, append-only late snapshots and same-coordinate control-region barriers, typed-value metadata, exact real/string/enumeration/physical/Logic9 encoding, owner-qualified aggregate/class/container/coverage/assertion leaf shape, resolved-strength state/ranks, atomic signal/UVM/dynamic-class/container/coverage/assertion/SDF observation fanout, deterministic time/delta/region/sequence/stable-id ordering, same-time change retention, versioned fixed-window/block compression, semantic/byte digest identity, governed hierarchy/provenance/selection/observation/event/timestamp/payload/value/profile validation, bounded declaration/initial/change/compression buffering, callback containment, flush/write/close/destructor failure containment, lifecycle finalization, staged publication, or restoration failed without publishing a partial output. The first terminal failure and diagnostic are retained across repeated finalization attempts, and failed or abandoned traces never report clean completion. Values that the selected FST profile cannot represent losslessly are rejected before publication. |
| `FSIM-TRACE-CONTROL-001` | error | Public trace configuration contains an invalid policy, lifecycle, output path, report limit, governed nonzero resource limit, or deterministic compression request for a non-FST format. |
| `FSIM-TRACE-CONTROL-002` | error | The requested trace format conflicts with the configured output extension. |
| `FSIM-TRACE-CONTROL-003` | error | A trace selection is empty, duplicated, individually oversized, or exceeds the governed aggregate selection-byte limit. |
| `FSIM-TRACE-CONTROL-004` | error | Trace selection or report configuration exceeds its governed entry limit. |
| `FSIM-TRACE-ARCHIVE-001` | error | A trace archive contains an invalid, stale, inconsistent, or duplicated profile, identity, selection, or declaration. |
| `FSIM-TRACE-ARCHIVE-002` | error | A trace archive envelope or payload is malformed, corrupt, truncated, trailing, future, or belongs to another artifact kind. |
| `FSIM-TRACE-ARCHIVE-003` | error | An archived trace format, profile, declaration selection, or relocation-safe output intent conflicts with the consuming request or another input artifact. |
| `FSIM-TRACE-ARCHIVE-004` | error | A trace archive exceeds a governed collection, text, byte, or relocation-path limit. |
| `FSIM-FST-READ-001` | error | An FST container is malformed, truncated, trailing, noncanonical, stale, or uses an unsupported clean-room profile. |
| `FSIM-FST-READ-002` | error | FST header, hierarchy, alias, geometry, timestamp, wave-chain, or typed-value data is internally inconsistent. |
| `FSIM-FST-READ-003` | error | FST input, decoded output, declaration, timestamp, value, text, metadata, or allocation use exceeds a governed reader limit. |
| `FSIM-FST-READ-004` | error | A canonical FST GZip or zlib stream has an invalid header, block, checksum, size, history reference, or padding. |
| `FSIM-FST-READ-005` | error | An FST input file cannot be opened or read completely. |

The [VCD and FST tracing guide](tracing.md) groups format selection,
transactional publication, public control/archive, and clean-room reader
diagnostics by lifecycle. Reader failures publish no partial trace and retain
one stable diagnostic from the five-code FST reader family.
| `FSIM-CACHE-0001` | error | A parsed source could not be hashed or associated with its cache provenance. |
| `FSIM-CACHE-0002` | warning | An unreadable or incompatible design-cache entry was discarded. |
| `FSIM-CACHE-0003` | error | A design-cache entry could not be populated. |
| `FSIM-CACHE-0004` | warning | Native-object cache load, store, or prune failures prevented complete cache reuse or eviction. |

## Non-project artifacts

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-ART-0001` | error | `.fsimobj` metadata has an unsupported, truncated, or trailing format/schema encoding; identity mismatches name found/required format and portable schema and direct current-build regeneration. |
| `FSIM-ART-0002` | error | `.fsimobj` metadata has an invalid language, standard, compatibility profile, source/unit index, path, checksum, or compilation digest. |
| `FSIM-ART-0003` | error | `.fsimobj` metadata or payload publication/loading failed, including overwrite, staging, permissions, or exact-payload-set failures. |
| `FSIM-ART-0004` | error | Explicit compilation is not one portable HDL source set, a checked source changed or became unreadable, or no owning unit was produced. |
| `FSIM-ART-0005` | error | Ordered object loading found no input, corruption, duplicate/colliding identities, inconsistent library or language-profile ownership, or an invalid restored semantic projection. |
| `FSIM-ART-VHDEP-001` | error | A portable object, design, or mapped-library artifact names a stale, unavailable, incomplete, or source-digest-mismatched compiler-supplied VHDL package dependency. |
| `FSIM-ART-0010` | error | `.fsimdesign` metadata has an unsupported, truncated, trailing, or runtime-ABI-incompatible encoding; identity mismatches name found/required format and ABI and direct current-build regeneration. |
| `FSIM-ART-0011` | error | `.fsimdesign` metadata has invalid roots, bindings, paths, checksums, object provenance, ordered VHDL or Verilog/SystemVerilog semantic-unit provenance, counts, or design digest. |
| `FSIM-ART-0012` | error | `.fsimdesign` publication/loading failed, including overwrite, staging, permissions, or exact-payload-set failures. |
| `FSIM-ART-0013` | error | A runtime, semantic, DesignIR, coverage, HIR, or UVM-checkpoint state payload is malformed, excessive, structurally invalid, contains an invalid scalar enumeration, or contains a producer-absolute source path; schema mismatches name the magic-specific found/required identity and direct `.fsimdesign` regeneration. |
| `FSIM-ART-0014` | error | Standalone design publication, inspection, loading, projection validation, exact semantic-unit language-profile association, fixed-delay compatibility, required-payload verification, or embedded SystemC producer-identity validation failed. |

## SystemC source compiler and plug-in validation

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-SCV-C001` | error | An SCV producer identity is missing, malformed, trailing, or differs in SCV release/header/source/patch/tree, SystemC runtime/source/bridge, TLM, compiler, standard library, adapter ABI, plug-in ABI, artifact schema, or cache schema. |
| `FSIM-SCV-A001` | error | A mapped, incremental, or embedded design artifact carries a missing, oversized, malformed, stale, or incompatible SCV producer identity; the native payload is rejected before it is opened or published. |
| `FSIM-SCV-B001` | error | An SCV backend island, hierarchy, object, stream, generator, sequence, or transaction identity is empty, unbounded, non-canonical, or lacks its required parent. |
| `FSIM-SCV-B002` | error | An SCV backend operation or receipt has an unsupported schema/enum/flag, invalid direction/status/correlation, missing or extraneous stable identities, invalid simulation coordinates, bad magic, or malformed framing. |
| `FSIM-SCV-B003` | error | SCV backend limits, fixed header, reserved bytes, message size, or bounded opaque payload are inconsistent or exceed the configured resource budget. |
| `FSIM-SCV-R001` | error | An SCV random seed path has a missing or malformed stable island, object, or canonical thread identity. |
| `FSIM-SCV-R002` | error | An SCV random distribution, bag, bound, exclusion, weight, or replay membership is invalid. |
| `FSIM-SCV-R003` | error | An SCV random identity, domain, draw, cycle, or replay resource limit is inconsistent or exhausted. |
| `FSIM-SCV-P001` | error | A native SCV smart-pointer kind, name, object identity, or opaque handle is invalid or stale. |
| `FSIM-SCV-P002` | error | A native SCV smart-pointer assignment or extension operation has an incompatible kind, path, index, or value type. |
| `FSIM-SCV-P003` | error | A native SCV smart-pointer handle, extension-depth, or generation resource limit is inconsistent or exhausted. |
| `FSIM-SCV-Q001` | error | An SCV constraint variable, target, domain, term, clause, distribution, solve order, arithmetic bound, solution, or publication is malformed or incompatible. |
| `FSIM-SCV-Q002` | error | A bounded SCV constraint problem is contradictory or otherwise unsatisfiable; no native value is changed. |
| `FSIM-SCV-Q003` | error | SCV constraint variable, clause, domain, search, or elapsed-work limits are inconsistent or exhausted; no native value is changed. |
| `FSIM-SCV-X001` | error | An SCV extension snapshot owner or hierarchy became invalid or stale before a complete snapshot could be published. |
| `FSIM-SCV-X002` | error | An SCV extension reports a malformed field, array-element, scalar-child, type, range, or value relationship. |
| `FSIM-SCV-X003` | error | SCV extension node, hierarchy-depth, value-width, plane-word, or string limits are inconsistent or exceeded; no partial snapshot is published. |
| `FSIM-SCV-T001` | error | A language-neutral transaction record has bad magic, an unsupported schema, or a missing stable stream, generator, or transaction identity. |
| `FSIM-SCV-T002` | error | A transaction record has malformed timing, typed-value shape, ordering, relation, correlated-object, reserved, framing, truncation, or canonical-high-bit data. |
| `FSIM-SCV-T003` | error | Transaction record message, collection, string, value-width, or plane-word limits are inconsistent or exceeded. |
| `FSIM-SCV-N001` | error | A native SCV database, stream, generator, transaction handle, stable identity, name, relation target, coordinate, or lifecycle state is invalid or stale. |
| `FSIM-SCV-N002` | error | A native SCV begin, end, attribute, or relation callback is missing, mismatched, unsupported, or produces an invalid common transaction record. |
| `FSIM-SCV-N003` | error | Native SCV stream, generator, handle, completed-record, attribute, relation, name, or record limits are inconsistent or exhausted. |
| `FSIM-SCV-L001` | error | SCV trace selection, observer, submission, close, or callback-reentry state carries an invalid identity, coordinate, limit, token, or lifecycle transition. |
| `FSIM-SCV-L002` | error | SCV trace correlation has duplicate or regressing transactions, invalid SystemC signal/port or TLM identity/time, invalid VCD/FST association, or a malformed resulting common record. |
| `FSIM-SCV-L003` | error | SCV trace selection, observer, correlation, waveform, sequence, or pending-record limits are exhausted; bounded backpressure rejects the submission without changing the queue. |
| `FSIM-SCV-W001` | error | An SCV transport envelope has bad magic/schema/framing, invalid island/sequence identity, an invalid common-record payload, or exceeds its message budget. |
| `FSIM-SCV-W002` | error | An SCV direct/worker-loopback transport has invalid kind/ownership, receives a foreign island, duplicate or regressing island sequence, or invalid/duplicate merge input. |
| `FSIM-SCV-W003` | error | SCV transport message, queue, byte, record, or merge limits are inconsistent/exhausted, or a disconnected/crashed transport cannot accept work; no queue state changes. |
| `FSIM-SCV-E001` | error | An SCV resource probe has an invalid island, empty workload, or out-of-range failure injection point. |
| `FSIM-SCV-E002` | error | An SCV resource probe could not contain or recover from an injected producer, consumer, recording, transport, solver, or ownership failure. |
| `FSIM-SCV-E003` | error | An SCV resource-probe transaction, attribute, solver, queue, message, byte, or memory-accounting limit is inconsistent, exhausted, or overflowed. |
| `FSIM-SC-A001` | error | A validated SystemC plug-in registered no module factory. |
| `FSIM-SC-A002` | error | A requested SystemC hierarchy has no compiled plug-in or registered factory. |
| `FSIM-SC-A004` | error | A typed SystemC factory failed during module construction or declared invalid HDL-proxy contents, bindings, or construction actuals. |
| `FSIM-SC-A005` | error | One SystemC instance path has conflicting factory targets. |
| `FSIM-SC-A006` | error | A registered SystemC object could not be bound to its common-runtime signal. |
| `FSIM-SC-A007` | error | The selected project time resolution cannot configure the SystemC runtime. |
| `FSIM-SC-A008` | error | A SystemC elaboration lifecycle callback failed or the selected lifecycle roots were inconsistent. |
| `FSIM-SC-C001` | error | The SystemC compiler working directory cannot be resolved. |
| `FSIM-SC-C002` | error | A SystemC source set contains no C++ sources. |
| `FSIM-SC-C003` | error | A SystemC source or dependency is missing, unreadable, invalid, or cannot be hashed/scanned. |
| `FSIM-SC-C004` | error | A compiler option conflicts with fsim's shared-library build contract. |
| `FSIM-SC-C005` | error | A SystemC plug-in cache/lock/staging directory cannot be created, inspected, or cleaned safely. |
| `FSIM-SC-C006` | error | The per-key SystemC plug-in cache lock cannot be acquired. |
| `FSIM-SC-C007` | error | The host compiler is unavailable, failed to start or complete, exited unsuccessfully, or produced no shared library. |
| `FSIM-SC-C008` | error | A compiled plug-in or its versioned key/size/checksum commit record cannot be hashed, written, or atomically published. |
| `FSIM-SC-C009` | error | A compiled SystemC plug-in failed image load, entry-point, ABI, initialization, or transactional registration validation. |
| `FSIM-SC-C010` | error | An explicit linked library is missing or cannot be content-hashed. |
| `FSIM-SC-C011` | error | A raw compiler option hides inputs from the persistent cache dependency model. |
| `FSIM-SC-C012` | warning | Compiler identity, dependency closure, a raw external input, or a volatile predefined macro prevents safe persistent plug-in cache reuse. |
| `FSIM-SC-C013` | error | A tracked plug-in input or compiler identity changed during compilation; the unpublished output was discarded. |
| `FSIM-SC-I001` | error | Incremental SystemC object or plug-in metadata has an unsupported, truncated, or trailing schema/producer encoding; mismatches name found/required format, ABI, SCV, and producer identities and direct regeneration. |
| `FSIM-SC-I002` | error | Incremental SystemC metadata has invalid ABI, toolchain, path, checksum, dependency, option, factory, schema, or digest values. |
| `FSIM-SC-I003` | error | Incremental SystemC artifact payload loading or transactional read-only publication failed, including checksum, overwrite, staging, or permission errors. |
| `FSIM-SC-I004` | error | One SystemC translation-unit compile or dependency scan is invalid, unavailable, mutated, unsafe, unsuccessful, or produced no object. |
| `FSIM-SC-I005` | error | Ordered SystemC object linking or native plug-in loading found duplicate/incompatible inputs, unsafe settings, invalid exports/entry points, a stale producer identity, ABI failure, or no published factories; producer diagnostics name found/required compiler, target, fingerprint, and version before native image open. |
| `FSIM-SC-I006` | error | Explicit SystemC plug-in inputs contain the same logical library more than once. |
| `FSIM-SC-B001` | error | A SystemC backend island, hierarchy, object, endpoint, transaction, or sequence identity has invalid canonical text, an absent parent, or a zero ordinal. |
| `FSIM-SC-B002` | error | A SystemC backend message has an unsupported schema/operation/direction/status/flag, invalid request-response correlation, incomplete identity chain, or malformed encoding. |
| `FSIM-SC-B003` | error | SystemC backend identity, payload, message, reserved-header, or protocol-limit resources are inconsistent or exceed their governed bounds. |
| `FSIM-SC-S001` | error | A SystemC kernel session request has the wrong owner, lifecycle phase, typed hierarchy identity, repeated construction, or terminal-state transition. |
| `FSIM-SC-S002` | error | A SystemC kernel session construction, object, binding, or lifecycle-receipt payload is malformed, truncated, trailing, inconsistent, duplicated, or noncanonical. |
| `FSIM-SC-S003` | error | SystemC kernel session plug-in path, identity, name, object, binding, parameter, detail, or enclosing protocol resources exceed their governed limits. |
| `FSIM-SC-S004` | error | Upstream SystemC context allocation, plug-in loading, native factory construction, typed port binding, elaboration, start, or zero-time quiescence failed and forced transactional rollback. |
| `FSIM-SC-E001` | error | A SystemC execution request targets the wrong session state, island, object, endpoint direction, order, or terminal transition. |
| `FSIM-SC-E002` | error | A SystemC scalar or typed input, advance, result, inspection, report, or snapshot payload is malformed, truncated, trailing, inconsistent, duplicated, noncanonical, or unordered. |
| `FSIM-SC-E003` | error | SystemC execution sample, typed value, detail, delta-cycle, duration, receipt, or enclosing protocol resources exceed their governed limits. |
| `FSIM-SC-E004` | error | Upstream SystemC value application, exact advancement, activity query, value sampling, pause/stop observation, or execution failed at a kernel safe point. |
| `FSIM-SC-L001` | error | A SystemC loopback request is malformed, uses the wrong direction, mutates a replay, or has a duplicate, stale, skipped, or exhausted island sequence. |
| `FSIM-SC-L002` | error | A loopback peer returned a rejected, malformed, excessive, misdirected, uncorrelated, wrong-operation, or wrong-identity response and was disconnected. |
| `FSIM-SC-L003` | error | SystemC loopback replay, buffer, or forwarded-exchange limits are invalid or exhausted. |
| `FSIM-SC-L004` | error | A SystemC loopback has no live serialized peer, or its peer disconnected or failed and was contained. |
| `FSIM-SC-N001` | error | A SystemC multi-island synchronization request targets an empty or terminal coordinator, or repeats, reverses, or skips the required exact time/delta safe-point order. |
| `FSIM-SC-N002` | error | A synchronized island, host language, endpoint, scalar input, serialized backend, or batch identity is invalid, unknown, or duplicated. |
| `FSIM-SC-N003` | error | SystemC synchronization island, input, output, batch, or request-sequence resources are invalid, exhausted, or exceed their governed limits. |
| `FSIM-SC-N004` | error | A serialized island failed to apply its input batch, reach exact time, drain evaluate/update/notification to quiescence, or return its ordered dirty-output batch; all coordinated islands were closed without partial publication. |
| `FSIM-SC-V001` | error | A type-erased SystemC value has unsupported or inconsistent kind, signedness, range, plane, padding, enumeration, or time metadata. |
| `FSIM-SC-V002` | error | A serialized SystemC value has a malformed magic, version, header, dimension, text, limb plane, truncation, reserved field, or trailing byte. |
| `FSIM-SC-V003` | error | A SystemC value-codec limit, width, encoded byte count, type name, enumeration table, literal, or aggregate enumeration text exceeds its governed bound. |
| `FSIM-SC-V004` | error | A type-erased SystemC value cannot be converted to or from the legacy scalar representation without losing width, range, kind, type, or state information. |
| `FSIM-SC-T001` | error | A native TLM1 endpoint, connection, interface kind, operation, transaction identity, or explicit-bridge ownership is incomplete, unsupported, noncanonical, duplicated, or inconsistent. |
| `FSIM-SC-T002` | error | A TLM1 transaction transition is stale or illegal, or native co-located traffic was submitted to the explicit serialized bridge. |
| `FSIM-SC-T003` | error | A TLM1 request, response, state, presence flag, header, reserved field, nested value, truncation, or trailing byte is malformed or noncanonical. |
| `FSIM-SC-T004` | error | TLM1 endpoint, peer, transaction, type-name, nested-value, or encoded-byte resources exceed their governed limits. |
| `FSIM-SC-U001` | error | Native TLM2 endpoint, socket, connection, transaction identity, direction, bus width, or explicit-bridge ownership metadata is incomplete, unsupported, duplicated, or inconsistent. |
| `FSIM-SC-U002` | error | A TLM2 transaction transition is stale or illegal, or native co-located traffic was submitted to the explicit serialized bridge. |
| `FSIM-SC-U003` | error | A TLM2 generic payload, command, response, phase, synchronization result, extension, DMI descriptor, header, reserved field, truncation, or trailing byte is malformed or noncanonical. |
| `FSIM-SC-U004` | error | TLM2 endpoint, peer, transaction, data, byte-enable, extension, or encoded-byte resources exceed their governed limits. |
| `FSIM-SC-W001` | error | A SystemC signal or primitive-channel inventory entry has invalid, duplicated, unordered, noncanonical, or inconsistent path, type, identity, domain, writer, update-owner, or observation metadata. |
| `FSIM-SC-W002` | error | A SystemC channel inventory was frozen or mutated in an illegal lifecycle state, or a frozen post-binding channel disappeared or changed. |
| `FSIM-SC-W003` | error | Unsupported SystemC channel metadata is inconsistent, or a custom channel claims support without an explicit observation adapter; unsupported custom channels remain visible in the immutable inventory. |
| `FSIM-SC-W004` | error | SystemC channel count, canonical path, type name, or encoded inventory bytes exceed their governed limits. |
| `FSIM-SC-X001` | error | A SystemC port or export inventory declaration has invalid, duplicated, unordered, noncanonical, or inconsistent identity, path, interface, kind, or direction metadata. |
| `FSIM-SC-X002` | error | A SystemC port/export binding does not resolve to an inventoried final channel, or its hierarchical, multi-bind, alias, or mixed-language chain is incomplete, repeated, cyclic, or unordered. |
| `FSIM-SC-X003` | error | A SystemC binding inventory was frozen or mutated in an illegal lifecycle state. |
| `FSIM-SC-X004` | error | SystemC binding declaration, target fanout, chain depth, path, interface name, or encoded inventory bytes exceed their governed limits. |
| `FSIM-SC-Y001` | error | A SystemC debugger, report, inventory-query, or TLM observation has malformed, inconsistent, duplicated, uncorrelated, or out-of-order metadata. |
| `FSIM-SC-Y002` | error | A SystemC debugger read, write, inventory query, or report was attempted outside the owning island's quiescent safe point. |
| `FSIM-SC-Y003` | error | A visible SystemC channel has no explicit debugger read or write adapter for the requested operation. |
| `FSIM-SC-Y004` | error | SystemC observation adapters, records, details, transaction payloads, or encoded batch bytes exceed their governed limits. |
| `FSIM-SC-Z001` | error | A SystemC trace route, frozen channel, binding alias, trace declaration, endpoint, value kind, width, or batch coordinate is invalid, duplicated, inconsistent, or out of order. |
| `FSIM-SC-Z002` | error | A SystemC trace selection, post-update submission, flush, or close operation was attempted outside the open trace lifecycle. |
| `FSIM-SC-Z003` | error | A lossless SystemC trace dirty batch or late-enable snapshot reached the governed pending-batch, value-count, or payload-bit bound and must be retried after flush. |
| `FSIM-SC-Z004` | error | VCD or FST rejected an accepted SystemC trace batch or failed during flush or close; the trace pipeline is terminal and retains the writer failure. |

SystemC incremental objects, native plug-ins, mapped-library variants, and
embedded design plug-ins retain one producer fingerprint over the exact
upstream source, compiler executable and environment, standard library, and
bridge revision. A consumer rejects or falls back from a producer whose current
fingerprint, runtime ABI, SystemC ABI, target, or CPU identity differs, even when
the artifact's internal checksums remain self-consistent.

Kernel-session responses carry the four stable session classifications as a
bounded enum while retaining a bounded human-readable detail. Failed native
mutation destroys every staged object and its owned upstream context before the
response is published; no partial island survives into a subsequent session.

## Consistency check

The `fsim.diagnostics-catalog` CTest scans literal `FSIM-*` identifiers in
`src/` and `include/`, removes the three cache-format markers named above, and
requires the resulting set to match this catalog exactly. It catches a new,
removed, or renamed literal code that is not reflected here.

The check is intentionally lexical. It cannot discover a diagnostic code
assembled dynamically at runtime, nor can it prove that the documented
severity and explanation still match every control-flow path. Current
production emitters use literal or constant literal codes, so the complete
current set is covered.

The Batch 162 UVM closure audit additionally freezes all 82 unique
`FSIM-UVM-*` codes inside the current 2,228-code catalog and requires explicit
race/deadlock, cancellation, stale/cross-owner, resource, callback/rollback,
and checkpoint/replay owners. The count is an inventory check; the individual
table entries above remain the authority for severity and meaning.

The Batch 163 VHDL/PSL closure audit freezes all 19 PSL codes in this table,
exact positive/negative/execution witnesses, and zero
unresolved active rows. Runtime PSL resource failures retain typed resource
kinds rather than synthesizing uncataloged diagnostic spellings.

Batch 166 older-mode execution uses the selected VHDL revision and Synopsys
package identity already established during analysis. The interpreter and
LLVM O0/O2 paths preserve exact composite bounds, direction, Logic9 planes,
time and delta ordering across VHDL/SystemVerilog/SystemC boundaries and
multiple roots. This closed execution surface introduces no new diagnostic:
revision, package-availability, overload-ambiguity and stale-artifact failures
remain owned by their existing catalog entries above.

The Batch 166 revision corpus freezes one exact older-standard availability
message and coordinate for each older standard: VHDL-87 rejects direct entity
instantiation at 2:7, VHDL-93 rejects a protected type at 2:17, and VHDL-2000
and VHDL-2002 each reject a context declaration at 1:1. The package corpus
also binds signed/unsigned ambiguity, stale `std_logic_arith` provenance and
incompatible `std_logic_misc` source to their single catalog rows. The registered
inventory validates the corresponding evidence anchors and prevents a
code-only or coordinate-only substitute from satisfying the corpus.

The registered Batch 166 closure matrix requires those diagnostics in the
same retained run as every older revision, all four Synopsys packages,
interpreter/LLVM O0/O2 execution, artifacts/caches, public boundaries and
cross-platform contracts. Its transcript contract includes the exact
mixed-package ambiguity code token. A passing child exit code
without the revision, package, engine, width, direction, null, provenance and
resource tokens cannot satisfy the matrix; each witness has its own retained
verbose log and result row.

Batch 166 public introspection carries the selected revision, predefined
environment, compatibility profile and compiler-package revision through
debugger scopes, execution activity, VCD comments and VHPI metadata. Invalid
partial VHPI provenance is rejected by the typed runtime service, while
compiler implementation packages remain absent from user hierarchy discovery.
This surface adds no diagnostic spelling: source/unit identity and exact wide
value/control evidence use the existing bounded public APIs.

Batch 167 compatibility profiles use the existing source-owned project
selection diagnostic for unknown switches and cross-family standards. Legal
switches canonicalize independently of the selected revision; parser defaults
retain their exact profile and do not authorize later grammar.
Verilog-2001-noconfig continues to use the existing structural-standard
diagnostic unless the explicit `configuration` switch restores only the
optional 2001 configuration surface.

Batch 167 public provenance adds no diagnostic code. Standard/profile
conflicts retain the standard-conflict diagnostic family, whose messages name the
canonical revisions and, for semantic-unit or package conflicts, both owning
compatibility profiles. The public C/C++/Tcl/debugger/VPI/VCD records report
the owning semantic unit and source without publishing compiler or cache
implementation identities; incomplete VPI provenance is rejected as invalid
typed metadata.

Batch 167 durable provenance adds no diagnostic code. Existing object/design
schema and consistency diagnostics reject a partial unit profile, a unit record
whose revision/profile disagrees with its object, or a design that omits an
owning Verilog/SystemVerilog semantic-unit record. These checks run before
standalone design publication; source hiding, cold/warm cache reuse, checkpoint
replay and relocation retain the original canonical identity rather than
inferring a current default.

Batch 167's published revision corpus freezes the representative negative
coordinates as follows: Verilog-1995 uses the declaration-form diagnostic at
1:21, Verilog-2001 the same diagnostic at 1:23, Verilog-2001-noconfig the
structural-standard diagnostic at 2:1, SystemVerilog-2005 the same structural
diagnostic at 1:21, SystemVerilog-2009 the declaration-form diagnostic at
1:25, and SystemVerilog-2012 the expected-identifier diagnostic at 1:45. Every
compatibility-switch row independently retains the declaration-form diagnostic
at 1:31 for the common later-grammar rejection. The TSV is the authoritative
mapping from those descriptions to the exact catalog codes.
The inventory gate also requires each row's positive, execution,
arbitrary-width, include/profile-provenance and artifact-mismatch anchors; a
matching code or coordinate without the remaining evidence cannot satisfy the
corpus.

## Release-status boundary

A stable diagnostic proves a bounded rejection contract, not a platform or
release result. Candidate support and unavailable evidence are recorded in
[`v2-support-matrix.tsv`](../packaging/v2-support-matrix.tsv) and
[`v2-release-record.txt`](../packaging/v2-release-record.txt). Batch 176 local
Linux Debug results must not be described as Windows, Release, sanitizer or
hosted-CI evidence; final Batch 177 owns those executions.

The Batch 167 serial closure matrix requires the corpus and diagnostic anchors
in the same retained run as older-mode frontend/application behavior,
interpreter/LLVM O0/O2 execution, cold/warm caches, artifacts and replay,
public C/C++/Tcl/VPI services, mixed-language execution and platform/resource
contracts. Its 16-row stage ledger and per-witness verbose logs are retained;
the matrix rejects a successful child exit if the six-mode, profile, width,
engine, artifact, hidden-producer or resource transcript tokens are absent.
