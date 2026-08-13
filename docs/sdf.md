<!-- SPDX-License-Identifier: Apache-2.0 -->
# Standard Delay Format support

Fsim's Batch 168 Standard Delay Format layer parses, normalizes, resolves and
persists SDF without changing runtime timing behavior. Verilog/SystemVerilog
timing application belongs to Batch 169 and VHDL/VITAL timing application
belongs to Batch 170. An accepted Batch 168 annotation therefore contributes
validated semantic and cache identity, not path delays or timing-check state in
a simulation.

## Supported input profiles

The clean-room frontend accepts SDF 4.0 directly and SDF 2.1 and 3.0 through
revision-specific adapters. It preserves ordered header spellings and values,
exact signed decimal and `min:typ:max` values, exact timescale conversion,
conditions, delay families, timing checks, timing-environment constructs and
empty, exact or wildcard instance selectors. Revision adapters reject
constructs, spellings, arities and ordering that do not belong to the selected
profile.

Parsing produces stable cell and node identities rather than host-sized delay
values. Normalization retains the source spelling and an exact canonical value;
conversion to femtoseconds is checked for loss and overflow. Lexer, IR and
resolution work is governed by explicit byte, token, nesting, node, mapping and
identity limits. See the [diagnostic catalog](diagnostics.md#standard-delay-format-frontend)
for the stable `FSIM-SDF-*` failure families.

## Resolution contract

An annotation is bound to an explicit elaborated-design root before cells are
resolved. Exact names follow the owning language's case and hierarchy rules;
wildcards select physical primitives only, and an empty selector denotes the
bound root. Resolution supports Verilog/SystemVerilog, VHDL and native SystemC
instances in one hierarchy.

The resolver maps cells, ports, nets, bit or part selections, interconnect
endpoints, specify paths, timing checks and conditions to stable elaborated
identities. Missing, ambiguous, duplicate, direction-incompatible or
width-incompatible mappings fail before an annotation can be published. The
result includes deterministic per-target and whole-annotation semantic
identities and counts for delay, timing-check and timing-environment records.

## C++ API boundary

The source-tree C++ interface is intentionally split by phase:

- `fsim/frontend/sdf.hpp` provides `lex_sdf`, `parse_sdf`, `normalize_sdf` and
  `lower_sdf_ir`, plus exact-value and immutable-IR types.
- `fsim/app/sdf_annotation_scope.hpp`, `sdf_cell_resolution.hpp`,
  `sdf_endpoint_resolution.hpp` and `sdf_mapping_validation.hpp` bind and
  validate an annotation against an `ElaboratedDesign`.
- `fsim/app/sdf_schema.hpp`, `sdf_artifact_identity.hpp` and
  `sdf_portable_archive.hpp` encode checked schema, cache and portable-artifact
  identity.
- `fsim/app/sdf_phase_persistence.hpp` installs or restores a validated
  portable annotation on a built project and publishes its design payload.

Every result carries diagnostics and reports success only when it has no error.
Callers may tighten the default resource limits. These C++ headers are an
internal/source integration surface in Batch 168; they are not part of the
installed stable C API. There is not yet a command-line option that applies an
SDF file to simulator timing. Batch 169 owns the Verilog/SystemVerilog CLI/API
application flow and Batch 170 extends it to VHDL/VITAL.

## Portable format and cache identity

The portable SDF payload begins with the `FSDFPORT` magic and schema version 1.
It contains the exact `DesignSdfAnnotation` identity, its schema envelope,
normalized cell/node records, resolved unit/object mappings and a checksum.
Decode is resource-bounded and rejects bad magic, unsupported schema, truncation,
trailing bytes, corruption and an expected-annotation mismatch before
publication.

Mapped `.fsimlib` entries and standalone `.fsimdesign` payloads use an `sdf:`
semantic identity. Design publication also composes the annotation into native
cache identity. The portable payload is sufficient for source-hidden and
relocated reload; the original SDF path is not consulted after a successful
load. Missing, stale or corrupt payloads reject instead of silently dropping the
annotation. `.fsimobj` unit provenance remains separate from the design-level
resolved mapping.

## Governed evidence

Clean-room corpora under `tests/fixtures/sdf/corpus/` cover SDF 2.1, 3.0 and 4.0
profiles plus exact Verilog, wildcard physical VHDL and native SystemC
resolution. `fsim.application.sdf_corpus` emits the stable
`FSIM-SDF-CORPUS-PASS` marker.

The `fsim.sdf-closure` test runs the frontend, adapters, normalization, IR,
scope, cell/endpoint mapping, schema, portable artifacts, project/library/design
reload, interpreter/LLVM, cold/warm/relocated cache and resource-portability
evidence. It retains `console.log` and `result.txt` under
`tests/sdf-closure-evidence/` in the selected build tree and requires the
`FSIM-SDF-CLOSURE-PASS` marker independently of subprocess exit status.
