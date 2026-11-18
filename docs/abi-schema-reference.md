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
| `.fsimobj` | one SQLite container (kind `fsim-object`) holding format 10, portable schema 16, compiled-HIR bundle 14 | canonical metadata, v3 code-coverage identity, compiled semantic/SV/VHDL HIR, and source paths (relative to the source root: the compiling workspace, or the base directory outside one; absolute only for a Windows source on another drive or share) with content digests and coverage facts, never source text |
| `.fsimdesign` | one SQLite container (kind `fsim-design`) holding format 18, runtime ABI 1 | roots/bindings/provenance, v3 code-coverage identity, the configuration settings it was elaborated with (key, value, origin), one checksummed hierarchy-path table shared by runtime and DesignIR, and checksummed runtime, semantic, DesignIR, HIR, coverage, UVM, class-specialization, SDF, trace, SystemC, and SCV state. Each payload checksum is `xxh64:` and sixteen lowercase hex digits (XXH64, seed 0; the integrity check simulate repeats at load) or lowercase SHA-256 hex |
| `.fsimlib` | one SQLite container (kind `fsim-library`) holding canonical TOML format 8, portable schema 16, compiled-HIR bundle 14 | logical-library metadata, compiled semantic/SV/VHDL HIR, source paths with content digests and coverage facts (never source text), and optional exact native accelerators |
| Workspace library catalog | SQLite schema 4 | artifact and dependency indexes with VHDL identifier-form columns; artifacts in analysis order (a recompiled artifact is the most recently analyzed), which selects an entity's latest architecture across files |
| `.fsimscobj` | one SQLite container (kind `fsim-systemc-object`) holding format 2, runtime ABI 1, SystemC ABI 4 | one C++20 translation unit, dependency identity, and native object |
| `.fsimscplugin` | one SQLite container (kind `fsim-systemc-plugin`) holding format 2, runtime ABI 1, SystemC ABI 4 | ordered object identities, link settings, sorted factory schema, and native shared library |
| Object caches | one SQLite database per cache (`static-kernel.sqlite3`, `compiled-hir.sqlite3` in the cache directory), application id `FSIM`, cache schema 1: one row per entry with its key, `xxh64:<16 hex>` payload checksum, size, last use and payload; a database of another cache schema is rebuilt; LLVM key schema `fsim-llvm-native-object-v169`; ordinary payload compiled-HIR bundle 4 | native-object and compiled-HIR caches; canonical schema-bearing lowercase SHA-256 key; 256 MiB payload ceiling; every load verifies the checksum and a corrupt entry is a miss |

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

The compiled-HIR bundle is schema 14 and embeds semantic schema 9,
SystemVerilog HIR schema 46, and VHDL HIR schema 20. SystemVerilog HIR
schema 46 types an array method iterator's `.index` and an event's or
sequence's `.triggered` (`Expression::builtin_method`, `BuiltinMethod::index`,
`triggered`, IEEE 1800-2023 7.12.4, 15.5.3), so no consumer tests the name's
text. SystemVerilog HIR schema 45 records the class or package a scoped type name selects from
(`TypeReference::class_scope`, IEEE 1800-2023 8.23), so linking never splits
a type's spelling, and the covergroup type-option markers
(`sv_coverage_type_option`, `sv_coverage_type_option_assign`, 19.7.1).
SystemVerilog HIR schema 44 keeps the string value of a constant `name` or `comment` option
assignment of a covergroup, coverpoint or cross
(`CovergroupOptionAssignment::evaluated_text`, IEEE 1800-2023 19.7). VHDL HIR schema 20
records an attribute specification's entity class and whether its entity
name list is `all` or `others` (`AttributeProfile::entity_class_kind`,
`name_list`, IEEE 1076-2019 7.2); the designator list then holds only named
entities, so an extended identifier `\others\` is a designator. SystemVerilog HIR
schema 43 marks the `default:` key of an assignment pattern
(`AssignmentPatternAssociation::choice_marker` `sv_default_choice`, IEEE
1800-2023 10.9), its unbased unsized fill literals by value
(`Expression::unbased_unsized`, 5.7.1) and the `new` of a dynamic array
constructor (`Expression::marker` `sv_new_array`, 7.5.1), and VHDL HIR schema 19 the `others` choice of an aggregate
and of a case, selected assignment or selected expression alternative
(`AggregateAssociation::choice_marker`, `Expression::marker`
`vhdl_others_choice`, IEEE 1076-2019 9.3.3, 10.9, 10.5.4, 9.3.8), so no
reader compares a choice's spelling. SystemVerilog HIR
schema 42 records the system task a system task call statement names
(`Statement::task_system_tf`) and the system function a constraint's call
names (`ConstraintExpression::system_tf`). Schema 41 records the result type of a
system function call as its clause gives it (`Expression::call_result_width`, `call_result_values`). Schema 40
records the system task or function a system call names
(`Expression::system_tf`; the lists of IEEE 1800-2023 20.1 and 21.1, 14.14,
16.14.7, 32.3 and Annex D), resolved once from its system_tf_identifier, so
a reader takes the routine from the call instead of its name. Bundle schema
14 (SystemVerilog HIR schema 39) records the method of a semaphore or mailbox
call (`Expression::builtin_method`: put, get, try_get, try_put, peek,
try_peek and num; IEEE 1800-2023 15.3, 15.4), resolved once from the member
name, and makes such a call written as a statement the member call with
its receiver first, so no consumer splits a task name or compares a
method's text. It also records the source-token ordinal of each procedure,
static initializer and continuous assignment (`Process::source_order`,
`Statement::source_order`), its place in the declaration order of its
design element, which orders the element's initialization random number
generator (18.14.1). Bundle schema 13
(semantic schema 9) records what each scope stands for
(`Scope::role`: a named scope, an unnamed block or fork, a loop's parameter
region, a case-matches item's pattern scope or an unlabeled process; IEEE
1800-2023 9.2, 9.3, 12.6, 12.7; IEEE 1076-2019 10.10, 11.3), so no reader
tests a scope's displayed placeholder name. Bundle schema 12
(semantic schema 8, SystemVerilog HIR schema 38) records the built-in method
a member call names (`Expression::builtin_method`,
`ConstraintExpression::builtin_method`: the string, enumeration, array,
queue and associative array methods, IEEE 1800-2023 6.16, 6.19.5, 7.5.1,
7.9, 7.10.2, 7.12), resolved once from the member name, so no consumer
compares a call's text with a method name, and the user-defined primitive a
holder unit stands for (`Unit::udp_holder`, `udp_primitive`, 29.1). Bundle
schema 11
(SystemVerilog HIR schema 37) records the content digest of each
SystemVerilog unit (`Unit::content_digest`), so the linker keeps one copy of
a unit that several objects compiled from the same text, and the driver slot
and instance of a gate or switch primitive's generated statements
(`Statement::gate_driver_slot`, `gate_driver_output`, `gate_driver_instance`)
instead of name suffixes. Bundle schema 10
(semantic schema 7, SystemVerilog HIR schema 36, VHDL HIR schema 18) carries
more of those kinds as typed fields: a predefined VHDL type mark is a plain
name with a `builtin` flag, the hidden DPI export imports and callables have
a typed role, a VHDL formal conversion says whether it is the driving or the
effective one, `type(expr)` has no spelling, and a generated property
evaluation's disable condition and guards are markers. Bundle schema 9 (SystemVerilog HIR schema 35, VHDL HIR schema 17) records more synthesized node kinds as typed markers where earlier schemas spelled them as `@`- or `__fsim_`-prefixed text: the constraint item forms (inside ranges and tolerances, dist weights, implication, if, foreach, unique, solve-before, disable soft; IEEE 1800-2023 18.5), the built-in methods of a class object (`randomize`, `rand_mode`, `constraint_mode`, `get_randstate`; 18.6-18.9, 18.13), the generated concurrent-assertion monitor calls and their placeholder actions (16.14, 20.11), and a VHDL array aggregate choice (IEEE 1076-2019 9.3.3.3); it marks the compilation-unit package and the imports of it (`Unit::compilation_unit_package`, `Import::compilation_unit_package`, IEEE 1800-2023 3.12.1), the hidden parameters of an interface port (`Declaration::interface_port`, `interface_member`), a generated assertion declaration (`AssertionDeclaration::synthesized`), an ambiguous VHDL literal and a user-defined attribute value (`Expression::ambiguous_literal`, `user_attribute_value`). Runtime 129 carries the class method hook of a container access, a checked cast, a string property access, a shallow copy, a process kill and a built-in object method (`ClassMethodCall::hook`, `ClassStaticMethodCall::hook`), how a class allocation runs its constructor (`ClassAllocate::construction`), the role, slot and dist weight of a constraint template (`SystemVerilogConstraintTemplate::role`, `slot`, `weight_kind`), the role of a debug local (`DebugLocal::role`), and the elaborator generator of a process (`Process::origin`); DesignIR 15 carries the generator of an elaborator-generated object (`Object::origin`), which VPI and traces leave out. Bundle schema 8 (SystemVerilog HIR schema 34, VHDL HIR schema 16) records the kind of every node the parser or a resolution pass synthesizes, such as a class property read, a container method, a cast, a streaming slice, a VHDL qualified expression or dereference, as a typed marker with its payload (`Expression::marker`, `marker_argument`; `Statement::task_marker`; `AssignmentPatternAssociation::choice_marker`, `AggregateAssociation::choice_marker`; `ConstraintExpression::marker` and its cast target), where earlier schemas spelled the kind as `@`-prefixed text in the node, so a node's text is never read as a kind; a built-in array method other than the named ones keeps its name as written beside its marker (`sv_container_method`, IEEE 1800-2023 7.12), an identifier that is a built-in type keyword records the type (`Expression::keyword_type`, a pattern key or a `$typename` operand, 10.9.2, 20.6.1), and a VHDL external name records its declared subtype's static width and value domain (`Expression::external_width`, `external_domain`, IEEE 1076-2008 8.7); runtime 128 carries the dense bit-plane image a filled sparse fixed array of packed elements keeps instead of its stored elements (`ContainerValue::compact_planes`, IEEE 1800-2023 7.4), the randomize step of a class method call as a typed hook (`ClassMethodCall::hook`, `randomize_no_properties`, IEEE 1800-2023 18.6.1, 18.11.1), the nominal element type of a container as a type index (`ContainerType::element_nominal_type_id`), the built-in type of a debug local, of a constraint variable, of a class random property and of a `std::randomize` target (`DebugLocal::systemverilog_builtin`, `SystemVerilogConstraintVariableProfile::builtin` with `nominal_type_id` and `state_property`, `SystemVerilogClassPropertyDescriptor::builtin`, `SystemVerilogClassRandomState::builtin`, `ScopeRandomizeTarget::builtin` and `nominal_type_id`, `ScopeRandomizeContainer::nominal_type_id`), and the target of a constraint template cast (`SystemVerilogConstraintTemplate::cast_target`, `cast_builtin`, `cast_width`). Bundle schema 8 (SystemVerilog HIR schema 34) also carries the built-in data type and net type a type reference names as typed kinds (`TypeReference::builtin` and `net_kind`, `AssociativeIndexTypeReference::builtin` and `net_type_kind`, IEEE 1800-2023 6.7 and 6.11-6.17) and the default net type of a compilation unit (`CompilationContext::default_net_kind`, 22.8), so no consumer tests a type by its keyword spelling; DesignIR 14 and runtime 128 carry the same kinds on packed type metadata and signals. Bundle schema 8 (SystemVerilog HIR schema 34) also records whether a call names a system task or function or a user subroutine (`CallKind`: `Expression::call_kind`, `ConstraintExpression::call_kind`, `Statement::task_call_kind`, IEEE 1800-2023 13.1, 20.1), so no consumer reads a leading `$` of a call's text, marks the loops and blocks a foreach statement is written as (`GeneratedStatementRole::foreach_layer`, no statement or scope of the source, 12.7.3) and a break that ends a foreach statement over several dimensions (`Statement::foreach_break`, 12.8); DesignIR 14 marks the root-relative lookup names of a single-root design (`Object::root_relative_alias`), which traces leave out (21.7). Bundle schema 7 (SystemVerilog HIR schema 33) carries the points of a concurrent assertion's generated monitor as typed statements (`StatementKind::sva_marker`, `SvaMarker`: an outcome or pending report with its directive, slot and name, the replayed pass and fail actions of a pending attempt with their severity and source, and the region and sampled-read points), where earlier schemas spelled them as in-band text (IEEE 1800-2023 16.14, 39.4.2). SystemVerilog HIR schema 33 also records what a statement the frontend generated for an assertion construct stands for (`Statement::generated_role`: the statement an expect statement expands into, the blocks of its pass and else statements, the statement that stands for a procedural concurrent assertion in its procedure, and a continuous assignment of a generated net), the property declaration of an expect statement's property spec (`Statement::expect_property`, `ExpectPropertyReference`: the unit or generate region whose assertion declarations hold it and its index), which nets, variables and processes the frontend generated (`Declaration::synthesized`, `Process::synthesized`), and the unnamed begin-end a procedure's statement was, whose declarations and statements the process holds (`Process::body_block`, its source), so VPI presents one vpiExpectStmt, no generated object, and the procedure's begin-end (IEEE 1800-2023 9.3.5, 16.14.6, 16.17, 37.12, 37.44, 37.73). Bundle schema 6 keeps, as package-member-use link references of a library object, the resolution of every name expression that leads to a package variable, net or parameter, so a reader of the object binds `pkg::v` to its declaration without resolving the spelling again (IEEE 1800-2023 26.3). Bundle schema 5 moves the declarations and statements after a package import in a function or task body into a nested block that carries the import, so the imported names are visible from the import on (IEEE 1800-2023 26.3, A.2.1.3), and keeps a real class value parameter whose actual is not an integer for the class specializer, which folds it to a real constant (6.20.2, 8.25). SystemVerilog HIR schema 32 also records the role in the governed UVM library (`support::UvmRole`) of every class, method, property and package declaration of the package `uvm_pkg` of a source set compiled with `--uvm-release`, and marks that package's unit (`ClassDeclaration::uvm_role`, `ClassMethod::uvm_role`, `ClassProperty::uvm_role`, `Declaration::uvm_role`, `Unit::uvm_role`); the roles are given once, by declared name, when the release's package is analyzed (IEEE 1800.2-2017, IEEE 1800.2-2020), and every later stage reads the role and never the spelling. SystemVerilog HIR schema 32 records, on each process built from a concurrent assertion statement, the origin of that statement (`Process::assertion`), so the VPI links an assertion to its process by identity and never by name, and records on each property or sequence declaration a checker instance expanded the checker instance and the declaration's name in the checker (`AssertionDeclaration::checker_instance`, `checker_name`), so the expansion is published under its source name (IEEE 1800-2023 17.2, 37.49, 37.53). SystemVerilog HIR schema 31 records whether a type reference was written as a type identifier (`TypeReference::named`), so linking rebinds only names a scope declares and never inspects the spelling of a keyword or inline type (IEEE 1800-2023 6.18, 8.25, 26.3). SystemVerilog HIR schema 30 records whether a type reference is the implicit placeholder of a declaration that names no type (`TypeReference::implicit`), so a user type named `implicit` is an ordinary named type (IEEE 1800-2023 6.18, 6.20.2, 13.3). SystemVerilog HIR schema 29 keeps the two bound expressions of such a gate instance array (`GenerateRegion::gate_array_left`, `gate_array_right`), which elaboration evaluates in each iteration's specialization, so the array's `vpiLeftRange` and `vpiRightRange` are the declared bounds whatever iterations are generated (IEEE 1800-2023 28.3.6, 37.35). SystemVerilog HIR schema 28 marks the loop generate a gate instance array with non-literal bounds expands to (`GenerateRegion::gate_array`): its block is no scope, and its primitives are the elements of the array (IEEE 1800-2023 28.3.6). Bundle schema 4 keeps a method call or task enable whose receiver is declared with a class type parameter in member-call form, the receiver first, so the method is chosen from the class the active specialization binds the parameter to (IEEE 1800-2023 8.25, 13.4.1). Semantic schema 6 adds `Expansion::outermost_macro`, the definition of the outermost macro a declaration was expanded from (macro name, content digest of the file holding the `` `define ``, line, column; IEEE 1800-2023 22.5.1), so the UVM library's own macros are told from user macros by the defining file, not by name. VHDL schema 15 adds `Expression::qualified_type_mark`, the type or subtype declaration the type mark of a qualified expression (IEEE 1076-2019 9.3.5) or of an external name's subtype indication (8.7) denotes where it is written (6.3, 12.3, 12.4), recorded at analysis so that no later phase resolves the mark's spelling again. VHDL schema 14 adds `SubtypeIndication::provided_type`, the predefined or IEEE type a type mark denotes as decided by visibility at analysis (IEEE 1076-2019 12.3, 12.4), so later passes identify these types without comparing spellings. VHDL schema 13 adds `Association::formal_conversion` and `Association::formal_conversion_calls`: a formal part of a binding indication's port map that converts an entity port (`f(s) => d`, IEEE 1076-2019 6.5.7.1, 7.3.2.1), marked by the linker's binding-map resolution, with the conversion call `f(s)` the elaborated implied block runs (14.5.4). VHDL schema 12 adds `Instance::linked_entity`, the unit the linker's entity reference of a direct `entity lib.name(arch)` instantiation resolved to (IEEE 1076-2019 11.7.1), which elaboration and the VHPI library model share. SystemVerilog schema 27 adds `Unit::default_decay_time` (the argument of `` `default_decay_time `` in effect where the module is defined, IEEE 1800-2023 E.2) and `Unit::default_disable_condition` (the condition of the unit's `default disable iff`, 16.15), for `vpiDefDecayTime` and `vpiDefaultDisableIff` (37.5). SystemVerilog schema 26 changes the frontend expansion of `run_test(name)`: the test is constructed by `__fsim_uvm_run_test_construct` in the calling process, so its constructor chain runs as generated code (IEEE 1800-2023 8.7), and units compiled before it are rejected. SystemVerilog schema 25 adds the match items of a sequence operand of the published property tree (`AssertionTreeOperand::match_items`: the assignment, increment, decrement or subroutine call statements of `(s, item, ...)`, IEEE 1800-2023 16.10, 37.54 `vpiMatchItem`). Schema 24 adds the constructor-chain argument expressions of `extends B(args)` (`ClassRelation::constructor_arguments`, IEEE 1800-2023 8.17, 37.32 `vpiArgument`). Schema 23 adds the named type packed dimensions were composed over (`TypeReference::composed_over`, IEEE 1800-2023 7.4.1), for the VPI typespec chain of a packed array of a typedef (37.25). Schema 22 adds the label of an immediate assertion (`Statement::immediate_label`, its `vpiName`, IEEE 1800-2023 37.49) and the clock events and default item of the property tree's case, clocked and multiclock nodes (37.52-37.54). Schema 21 adds the published operator tree of a property or sequence (`AssertionOperator` nodes and their operands, the VPI property and sequence expression model of IEEE 1800-2023 37.50-37.54). Schema 20 adds the `iff` guard of one event of a multi-event control (`Sensitivity::guard`, IEEE 1800-2023 9.4.2.3). The standalone design's
other current state schemas are runtime 136, DesignIR 21, coverage 10, UVM 3, and
class specializations (`FSIMSVCS`) 8. Runtime 136 keeps the Preponed
values and sampled-value histories of the signals a static kernel's
SystemVerilog members sample in the kernel's arena (IEEE 1800-2023 16.5.1,
16.9.3), saved with the kernel; the host keeps none for those signals.
Runtime 136 also gives a `$sampled(h.p)` site's generated signal the handle
chain it samples at the start of each time slot (`Signal::sampled_property`,
`SampledPropertyChain`, 16.5.1).
Runtime 135 carries which value of its
design element's initialization random number generator seeds each process
(`ElaboratedDesignState::process_random_ordinals`, IEEE 1800-2023 18.14.1).
Runtime 134 marks the read of an update
statement's own associative-array element (`ContainerRead::allocating`), which
allocates a missing entry before referring to it and so reads it without the
nonexistent-entry warning (IEEE 1800-2023 7.8.6, 7.8.7), and a mailbox of
container messages (`MailboxElementKind::container`,
`MailboxPut::container_source`, `MailboxGet::container_destination`): a
message of an unpacked array or union type, or of an unpacked structure type
with a member that is not a packed or scalar leaf, is one container value
(15.4). Runtime 133 carries the named scopes
that enclose a debug local and its simple name (`DebugLocal::scope_path`,
`leaf`), so a debugger builds its scopes without splitting the displayed
name. Runtime 132 carries the frame a debug
local belongs to as a typed record (`DebugLocal::frame`, `DebugFrameLocal`:
the callable lowering and declaration, the variable, its role as a variable,
formal, receiver or result) and the lowering of the callee of a frame entry
or exit point (`DebugPoint::callee_lowering`), where earlier schemas spelled
them in the local's name, and the message type of a put or get on a
typeless mailbox (`MailboxPut::message_type`, `MailboxGet::message_type`,
`MailboxElementKind::dynamic`, IEEE 1800-2023 15.4.9). DesignIR 21 gives
`CoverageQuery` the item it queries (a coverpoint's or cross's coverage,
IEEE 1800-2023 19.9) and `CoverageOption` a nonblocking flag, set in the NBA
region (10.4.2), and a type scope (`type_option`, 19.7.1); coverage state 10
keeps each covergroup type's type options as procedural assignments left
them (`SystemVerilogCoverageState::type_options`). DesignIR 20 adds the
SimIR operation `CoverageOption`, a read or procedural assignment of an
instance option of a covergroup instance, its coverpoints and its crosses
(the instance's runtime identity, the item, the option and an integer or
string register, IEEE 1800-2023 19.7, Table 19-1), and coverage state 9 keeps
each instance's options as its construction and procedural assignments left
them (`SystemVerilogCoverageState::options`), from which instance and type
coverage are weighted (19.11.3). DesignIR 19 drops the
instance-weighted overall coverage query, whose only source was the
`$get_inst_coverage` function that IEEE 1800-2023 does not define
(`CoverageQueryKind` keeps the overall type query and the covergroup type and
instance queries, 19.8, 19.9). DesignIR 18 marks the
storage the elaborator generates for VHDL protected objects, files and
access values (`GeneratedObjectOrigin::vhdl_support_storage`), which VPI
leaves out. `FSIMSVCS` 8 carries the built-in method of a member call in its
expressions. Runtime 131 carries the driver slot and
instance of a primitive's processes, a tagged union's hidden tag member as a
flag (`ContainerType::tagged_union`), and a DPI call's argument and result C
types as typed entries (`DpiArgumentType`, `DpiCTypeCode`) instead of a
descriptor string. `FSIMSVCS` 7 keys an anonymous type actual by its HIR type
id. DesignIR 17 marks a process VPI publishes under a name the tool made
(`ProcessOccurrence::generated_name`, the driver of a procedural continuous
assignment). Runtime 130 types a DPI call
(`ClassStaticMethodCall::call_kind` and `dpi`), the role of each call actual
(`actual_roles`), whether a debug point's callee receiver is an instance path,
and the builtin flags of signals and container element types; it drops the
non-LRM `ProcessCompleted` operation. DesignIR 16 adds the generated origins
of port adapter processes. Coverage 8 keys an instance's configuration,
aggregate and specparam parameters by kind. `FSIMSVCS` 6 keys `type(expr)`
and predefined-type actuals without their old text prefixes. `FSIMSVCS` 5 carries the UVM library role of each class specialization and of its methods and properties (`ClassSpecialization::uvm_role`, `SpecializedClassMethod::uvm_role`, `SpecializedClassProperty::uvm_role`), and saves a class value parameter bound to a constant of the synthetic constant pool as the constant's kind and text (`SpecializedClassParameter::constant_kind`, `constant_text`) instead of its pool id, which the loading process interns again, and identifies a real value parameter by the IEEE 754 binary64 bit pattern of its value (`real:<16 hex>`), so 2.5, 2.50 and 5.0/2 are one specialization (IEEE 1800-2023 6.20.2, 8.25). DesignIR 13 marks a process occurrence the frontend generated for an assertion construct (`ProcessOccurrence::synthesized`: an expect statement's or a concurrent assertion's clock, enable or consume process, IEEE 1800-2023 16.12, 16.17), which has no VPI process object (37.63). DesignIR 12 also records the typedefs each class declares as the typespecs a use of each has in the declaration and in every specialization whose value parameters all have integral values (`SpecializationInfo::vpi_class_typedefs`, IEEE 1800-2023 37.25, 37.32), and the typespec such a specialization gives each of its properties (`vpi_class_property_typespecs` entries with the specialization identity, 37.32 detail 5). DesignIR 12 adds the concurrent assertion statement an assertion process occurrence was built from (`ProcessOccurrence::assertion`, the origin of the HIR statement), which the VPI follows from an assertion object to its statement and its process (IEEE 1800-2023 37.49). DesignIR 11 adds to the call frame of every callable local the storage table that holds it (packed, string or container) and whether its declared type is an event, binds the formals and locals of SystemVerilog tasks and functions as well as VHDL subprograms, and records in each signal the array of named events it is an element of (declared name, array typespec, position), so VPI publishes the array as one object (IEEE 1800-2023 7.4, 15.5, 37.43). DesignIR 10 adds the call-site to frame-local-set map of a process (`Process::call_frames`): for every call site (a function call's expression, a procedure call's statement, a concurrent procedure call's statement) the frame it runs and the frame whose body holds the call, and for every frame the debug-local and frame-local slots of its formals, variables and loop parameters by HIR declaration, which the VHPI reader follows from the process down the call stack (IEEE 1076-2019 14.6, 19.4.3, 22.5.2). Schema 127 adds the SimIR operation `HostRequest`, a typed request a process makes of the run host: a concurrent assertion outcome or pending report (with the replayed actions), `$printtimescale`, a scope task (`$scope`, `$showscopes`, `$showvars`), a simulation control task (`$save`, `$incsave`, `$restart`, `$reset`), an assertion control task or a coverage control call, with up to four integer arguments read from registers; it replaces the in-band text markers that `OutputCallback` carried. `FileOpen` gains `multichannel` (an `$fopen` without a mode opens a multichannel descriptor, IEEE 1800-2023 21.3.1), and a scan conversion names its VHDL TextIO read form (`InputScanConversion::vhdl_textio`, IEEE 1076-2019 16.4). Schema 126 also marks the resolver of a net of a user-defined nettype (`VhdlSignalResolver::systemverilog_nettype`): its program calls the net's resolution function over the values of the net's drivers as a dynamic array (for an array of nettype nets, once per element over the drivers of that element) and runs as an update of the net, the first at time zero, which a restart does not repeat (IEEE 1800-2023 6.6.7, 6.7.3). Schema 126 also saves the analysis implementations of the run (`uvm_tlm_implementations`): the port objects whose library port-base constructor was given the implementation port type, the last literal of the library's `uvm_port_type_e` (IEEE 1800.2-2020 13.1), so a `uvm_analysis_imp_decl` class delivers writes after a restore. Schema 126 adds to each specialization the let expressions of its unit with the body expression of the let declaration each expands (`SpecializationInfo::vpi_let_uses`, IEEE 1800-2023 11.12, 37.57), and the callable each user task or function call of its unit is bound to, as the lowerer bound it: the call expression or task call statement, the callable's declaration and the instance that owns it (`SpecializationInfo::vpi_callees`), which the VPI follows for `vpiFunction` and `vpiTask` of a call (37.42, 37.38) instead of looking the name up. It also adds the parameters of each class declared in the specialization's unit (`SpecializationInfo::vpi_class_parameters`): for the class defn and for each class specialization whose value parameters all have values, every parameter of its parameter port list and of its body with its value as a recorded constant, its typespec and whether it is a type parameter, whose typespec is then the bound type (`VpiPackageParameterInfo::specialization`, `type_parameter`), which the VPI reads for the value, `vpiTypespec` and `vpiParamAssign` of a class parameter (IEEE 1800-2023 37.31, 37.32). Schema 125 adds the SimIR operation `EventArrayElement`, a wait, trigger (blocking, or nonblocking with an optional delay) or triggered-state query on the element of a named-event array that a run-time index selects: the operation carries the index register, its width and signedness, the array's events from its left index with the direction of its range, and the destination of the triggered state; an index with an unknown bit or outside the range selects no element (IEEE 1800-2023 7.4, 7.4.6, 15.5). Schema 124 VPI class property typespecs are recorded per class specialization, for every specialization whose parameters all have values, and interface typespecs list every parameter assignment. Schema 123 makes the SimIR operation `WriteContainerObjectString` carry a leaf member path, so a nonblocking assignment replaces a whole element of a nested container, or a container member of a struct element, with a value captured at the statement (IEEE 1800-2023 10.4.2, 7.8-7.10). Schema 122 makes the SimIR operation `WriteContainerObjectString` carry a source kind (text, packed or container) with a packed and a container source, so a nonblocking assignment captures a numeric, struct, pattern or container value for an element of an associative array, queue or dynamic array and writes it in the NBA region (IEEE 1800-2023 10.4.2, 7.8-7.10). Schema 121 adds the parameter assigns of an interface typespec (`SystemVerilogVpiTypespec::param_assigns`, IEEE 1800-2023 37.30), and gives a package that declares variables a design specialization of its own, so VPI publishes its variables (37.10, 37.17). Schema 120 makes the kernel state section (version 4) carry the signal histories of kernel-owned slots: the update-cycle count, the time and cycle of each history's latest event and transaction, and the value each slot's histories last saw (IEEE 1076-2019 16.2.4). Schema 119 makes the SimIR operation `WriteContainerObjectString` carry the nested container path of its string element: one level per container index or key, each with the member path into its element, so a write reaches a string in a nested container or in a container member of a struct or class (IEEE 1800-2023 7.10, 7.12, 10.4.2). Schema 118 adds the SimIR operation `WriteContainerObjectString`, a write of one character of a string element of a container object, whose nonblocking form publishes in the NBA region (IEEE 1800-2023 6.16, 7.10, 10.4.2). Schema 117 follows the class identities of real-parameter specializations, which saved dynamic class types name. DesignIR 9 fills the driving drivers of every inout formal with a conversion or of a resolved subtype (`VhdlSignalResolver::driving_drivers`): the actual's converted effective value is the formal's effective value and never a source of it, and the formal resolves its own drivers into the conversion that drives the actual (IEEE 1076-2019 6.5.7.1, 14.7.3.2, 14.7.3.3). `FSIMSVCS` 4 binds a class value parameter whose actual reads the instantiating module's parameters, or folds operators over other values, to a constant of the design's synthetic constant pool that holds the folded value, so specializations with equal values are one type whatever expressions produced them (IEEE 1800-2023 6.22.1, 8.25); each published instance specialization names the specialization of its own instance. Schema 116 keeps in the foreign run state the value each call of a system function with state (`$random`, `$urandom`, `$urandom_range`, the `$dist_` functions) last returned under `simulate --foreign-probes` (`ForeignRunState::call_results`, keyed by the call's source position), so `vpi_get_value` of the `vpiSysFuncCall` after a restart reads it (IEEE 1800-2023 37.58, 38.15). The foreign sections (`foreign.vpi`, `foreign.vhpi`) are at version 3, whose call results also hold string and container results; `foreign.vpi` is followed by the delay edits of the VPI service. Schema 115 Saved dynamic class types and class specialization recipes can name those pool constants. `FSIMSVCS` 3 identifies a class-type parameter actual by its class declaration and specialization only, so an explicit actual equal to the default names the defaulted specialization (IEEE 1800-2023 6.22.1, 8.25). Schema 114 The run state keeps the forces of the debug interface on VHDL variables (`Impl::debug_local_hooks`, IEEE 1076-2019 22.5.2), and a compiled VHDL member's process variables are saved with its reference registers. Schema 113 adds the resolution program of an inout formal's driving value (`VhdlSignalResolver::driving_resolver`, `driving_drivers`): a formal of a resolved subtype associated with a signal takes its effective value from the actual and gives the function's result over its own drivers to the actual as one source, through a conversion driver of the actual that the runtime assigns in the same simulation cycle (IEEE 1076-2019 14.7.3.2, 14.7.3.3). Schema 112 elaborates the pre_randomize and post_randomize callbacks of randomize() as generated code (IEEE 1800-2023 18.6.1). Schema 111 records the UVM-derived objects a lowered `type_id::create` or `run_test` has allocated whose constructor chain has not yet reached the library (`uvm_pending_constructions`, empty at every save point); `@uvm-run-test-begin` no longer creates the named test (`@uvm-run-test-construct` does), and the lowered constructor chain of a UVM-derived class reaches the library through `@uvm-construct-base` (IEEE 1800-2023 8.7, 8.17). Schema 110 adds the null flag of a VHDL waveform element (`ProjectedWaveformElement::null`, `ProjectedWaveformValue::null`: 1 for a bus, 2 for a register; a null transaction disconnects the driver of a guarded signal, IEEE 1076-2019 10.5.2.2, 14.7.3.2) and the value of a bus resolution function over no sources (`VhdlSignalResolver::null_value`, evaluated at elaboration, 6.4.2.3), and in the run state the disconnected elements of each guarded driver and the values that registers keep. `FSIMSVCS` 2 numbers the virtual method slots of a specialization after the methods it inherits from its base specialization, so a class that extends a type parameter takes its slots from the actual base (IEEE 1800-2023 8.20, 8.25). Schema 109 adds the block and generate statement occurrences a VHDL instance elaborated (`SpecializationInfo::vhdl_generate_occurrences`: the path relative to the instance, such as `g[1]` and `h[0].e[3]`, and the declaration of the region whose occurrence it is, which tells the first alternative of an if generate statement from its else and elsif alternatives and names the chosen alternative of a case generate statement), so an occurrence that holds no signal, instance or process is still a region of the VHPI design hierarchy model (IEEE 1076-2019 14.5.3, 19.4.2). DesignIR 8 adds the `dormant` flag of a driver region (`Process::DriverRegion::dormant`): the placeholder process of a foreign architecture holds a dormant region for each signal and each out, inout or buffer port of the instance, which is no source of the signal until the elaboration function creates the driver (IEEE 1076-2019 20.4.2, 23.6, 14.7.3.1). DesignIR 7 adds the copy drivers of every run of bits that an SDF net delay record owns (`SdfNetDelayInfo::run_drivers`; a put on the record retimes each run, IEEE 1800-2023 38.32) an `increment` flag on an SDF net delay record that adds to the bits of an earlier record of the same source and port (`SdfNetDelayInfo::increment`), and the source extent of the statement each concurrent SystemVerilog process was lowered from (`SpecializationInfo::statement_sources`, which the VPI driver of a net matches against its continuous assignments, 37.21), and the frame-local table of a process (`Process::frame_locals`: the formals and locals of a VHDL function and the parameters of `for` loops of a subprogram, which a VHPI program reads while the call runs; IEEE 1076-2019 14.6). DesignIR 6 adds the call-result table of a process (`Process::call_results`: the slot that holds each function call's returned value, which `vpi_get_value` of a `vpiFuncCall` reads). Schema 108 adds the class specialization a static class method call runs in (`ClassStaticMethodCall::class_specialization`): each specialization of a parameterized class has static properties and methods of its own (IEEE 1800-2023 8.25.1), so a call through `C#(4)::f()` or a typedef of it reads and writes C#(4)'s statics. It also adds the disable net of a concurrent assertion (`SvaOperation::disable_signal`): a `disable iff` condition is kept in a hidden net by a continuous assignment, and the run disables every attempt in flight when the net becomes 1, at once and with current values (IEEE 1800-2023 16.12); an expect statement's monitor reports the disabled outcome then, which ends the statement (16.17). It also adds the wake bit, the abort net and the monitor flags of a monitor that also evaluates at time steps without a tick of its clock (`SvaOperation::wake_signal`, `abort_signal`, `monitor_flags`): asynchronous `accept_on`/`reject_on` conditions are checked with sampled values at every time step of an attempt (16.12.14), and a disable condition with a sampled value function or a triggered state changes value at the start of a time step (16.9.3, 16.12), so the run toggles the wake bit at the start of the next existing time step after the abort net changed, or of every time step for such a disable condition, while an attempt is in progress. Schema 108 also carries the local variables of concurrent properties (IEEE 1800-2023 16.10): the plan text "sva5" (`semantic::sva::Plan::locals`, `expressions`, `local_sets`, a property's `guard_expression` and `initializers`, the count of subroutine-call match items `Plan::calls`, and the instructions `check_expression`, `assign` and `call`), the monitor operations `SvaKind::request`, `SvaKind::local`, `SvaKind::answer` and `SvaKind::call`, whose `start` is the request number (`0xffffffff` for a local of the subroutine call `SvaKind::call` took last), whose `guard_count` is the local's index or the answer's width and whose `guards` is the answered value, and attempt block layout 7 (`0x5356'4131'0007`, header words 49): the memo region of the local-variable expressions (`h_memo_off`, `h_memo_used`, `h_memo_cap`, `h_memo_stride`, `h_memo_pending`), the queue of subroutine calls a tick reached (`h_calls_off`, `h_calls_used`, `h_calls_cap`, and `h_calls_taken`, the words of it the process took so far, whose last entry is the call `SvaKind::call` took last; each entry is the call's index and the thread's locals, run by the process in the Reactive region in match order, IEEE 1800-2023 16.11), a join end of a matcher record that is the end tick and then the thread's locals (`1 + locals_words` words), and an instance record whose plan has locals carries the locals after its header words (flag 2048 when its initialization assignments ran); a thread's locals follow its repetition counters. Schema 108 also carries the default of an associative array in the container values of the design (`ContainerValue::associative_default`, `associative_default_text`), so a static array declared with `'{default: v}` returns v for a key with no entry when the run starts (IEEE 1800-2023 7.9.11). Schema 107 replaces the outcome-start table of the interpreter's assertion host by a pop serial (`sva_pop_serial`; the start is `h_last_start` of the attempt block) and moves the attempt block to layout 2 (fail position and pop serial in the header, step records of ten words with the guard's source position). Schema 106 adds the pass and failure branches of an immediate or deferred assertion that a probe run reports (`assertion_pass`, `assertion_fail`; IEEE 1800-2023 39.4.1) and stores the attempt state of concurrent assertions in one flat block per monitor (`sva::AttemptStorage`). Schema 104 stores the HIR expression of a primitive terminal and the `inert` flag of an output operation that holds a concurrent assertion's attempt-start report, which a run binds when a library first registers `cbAssertionStart` (no persisted state depends on it). Schema 103 keeps the wire-delay parts of a
VITAL delay call (`VitalWireCall`: the interconnect generic, the sources of
the multiple source mode, the mirrors, and the element offsets) behind a
presence flag, so a call without them stores one byte. Runtime schema 101 stores packed values as
their width and raw bit planes. Schema 102 adds the interconnect-delay write
`WriteNetDelay` (twelve transition delays with a pulse reject and error limit
each, IEEE 1364-2005 16.2.4) after the signal operations, the pulse limits
of an SDF net delay record, and the table of reads through hierarchical names
(`hierarchical_reads`: process, ReadSignal destination register, signal,
implicit-wait flag and full path) that SDF net delays use to give such a read
of a delayed load port the delayed value (IEEE 1364-2005 16.2.4). Schema 101
gives a user-resolved VHDL signal
that a foreign architecture drives a resolver program that takes the driver
count at run time, after its eight fixed-count variants, also for a composite resolved per element or by segments, and records the typespec of each user-defined nettype declaration for VPI. Schema 100 adds the virtual interface handle
table (IEEE 1800-2023 37.29 `vpiActual`) and the definition name of an
interface typespec. A debug point carries the callee it belongs to (schema 99); from runtime schema 126 a frame-entry point also carries the callee's HIR declaration and the receiver path of a call into another instance's interface (`DebugPoint::callee_declaration`, `callee_receiver`), from which the VPI identifies the task or function a frame runs (IEEE 1800-2023 37.43, 38.36.1) instead of comparing names, and the point kinds
`frame_entry`, `frame_exit` and `class_constructed` mark the frame of a
SystemVerilog callable and a completed constructor for the execution probes.
The standalone `FSIMSVCH` constraint-HIR state
is schema 17; schema 15 marks an `interconnect` net's type reference (IEEE 1800-2023 6.6.8) for the VPI model, and schema 16 adds a statement's immediate assertion kind and deferral (16.4) and the `foreach` record of a desugared loop (12.7.3) for the VPI statement model. Schema 17 adds the events of an assertion clock, the condition of a `disable iff`, and the pass and fail action statements of a concurrent assertion, for the VPI assertion model (IEEE 1800-2023 37.50). The runtime schema carries opaque design-local package owner IDs on package
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
