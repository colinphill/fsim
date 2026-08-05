<!-- SPDX-License-Identifier: Apache-2.0 -->
# v1 legality closure audit

This is the Batch 127 working inventory for eliminating silently accepted,
discarded, parser-only, and under-diagnosed required constructs. The
[feature matrix](feature-matrix.md) remains the release authority; this file
groups every row that is not yet `execute` so each one has an explicit Batch
127 workstream. Deferred and post-v1 rows are excluded.

## Baseline

The current shared matrix contains 1,082 required rows:

| Current status | Rows |
|---|---:|
| `execute` | 1,082 |
| `analyze` | 0 |
| `elaborate` | 0 |
| `parse` | 0 |
| `v1 target` | 0 |
| nonstandard `metadata` | 0 |

All 1,082 rows are `execute`, and every P+, P-, E, and R evidence cell is
populated. The gate rejects any status regression, evidence dash, duplicate
ID, undocumented status, missing path-like evidence link, or loss of the exact
language row counts. Task 8 reclassified the former undocumented `SC-024`
status, and Task 9 closed the final common/release rows.

## Execute-status evidence-gap queue

The queue is empty. Task 9 filled the final common and cross-cutting gaps.

## VHDL queue — Batch 127 Tasks 5 and 6

Task 5 closed 97 non-execute declaration/type/binding rows plus four
execute-status negative-evidence gaps. The gate now protects 242 of 252 VHDL
rows as `execute` with complete evidence, covering library units, contexts,
declarations, types, names, overload declarations, every generic family,
ports, components, configurations, analysis order, guarded blocks, and
generated specialization.

Task 6 closed its exact ten-row expression/callable set: execute-status
negative gaps `VH-006`, `VH-007`, `VH-008`, `VH-010`, and `VH-018`;
parser-only name/call/selection row `VH-013`; and callable/expression legality
rows `VH-203`, `VH-205`, `VH-206`, and `VH-207`. The legality gate now requires
all 252 VHDL rows to remain `execute` with complete P+, P-, E, and R evidence;
there is no remaining VHDL legality queue.

## Verilog/SystemVerilog closure — Batch 127 Tasks 3 and 4

Task 3 closed 53 declaration/type status rows spanning non-ANSI ports, integral,
type, and string parameters, static/dynamic hierarchy ports, slice-port
actuals, interfaces/modports, package re-exports, preprocessing/directive
state, generated declarations, and aggregate/multidimensional types. Every
closed row became `execute` with complete positive, negative, elaboration, and
direct runtime evidence. Task 4 closed the other 120 non-execute status rows
plus every pre-existing execute-status evidence gap. The legality gate now
requires all 697 SystemVerilog rows to remain `execute` with complete P+, P-,
E, and R evidence; there is no remaining v1 SystemVerilog legality queue. V2
Batch 147 adds ten executable class-foundation rows, `SV-698` through
`SV-707`, with complete owning-HIR, negative, runtime, engine, debugger, and
artifact evidence. The current gate therefore requires all 707 SystemVerilog
rows and 1,137 total matrix rows to remain executable with no evidence gaps.

## Mixed-language and SystemC queues — Batch 127 Tasks 7 and 8

- Task 7 closed parser-only manifest binding row `ML-001` and the negative-
  evidence gaps on `ML-002` through `ML-004`. V2 Batch 134 adds `ML-017` for
  configurable logical-library search; all 17 mixed-language rows are now
  gated as executable with complete evidence.
- Task 8 reclassified `SC-024` from undocumented `metadata` to `execute` and
  filled the three earlier SystemC evidence gaps. All 28 SystemC rows are now
  gated as executable with complete evidence.

## Common and release-contract queue — Batch 127 Tasks 2 and 9

The remaining 12 rows are `CM-012`, `CM-016`, `CM-020`, `V1-CM-02`,
`V1-CM-03`, `V1-CM-04`, `V1-CM-05`, `V1-CM-06`, `V1-CM-07`, `V1-CM-08`,
`V1-CM-09`, and `V1-CM-10`.

Task 9 promoted `CM-012`, `CM-016`, `CM-020`, and `V1-CM-02` through
`V1-CM-10`, then filled every common execute-status gap. All common and
release-contract rows are now executable with complete evidence.

## Evidence-gap and silent-discard rules

Batch 127 uses these closure rules:

1. An accepted required construct must create an owning semantic/HIR record or
   an explicitly validated runtime projection; merely consuming tokens is not
   evidence.
2. An unsupported or illegal bounded form must emit a stable cataloged
   diagnostic at the earliest layer with enough type and specialization
   context to decide legality.
3. Error-recovery parses may discard a temporary result only after recording a
   targeted diagnostic. Optional punctuation matches and deliberate HIR
   interning calls are not semantic discards.
4. Status-only promotion requires verifying that every cited link exists and
   that R evidence directly executes the bounded row. Presence of a nonempty R
   cell alone is not sufficient.
5. Rows without meaningful runtime semantics still retain an R evidence gap
   until the matrix contract is explicitly extended with a documented
   non-applicable state; Batch 127 does not silently reinterpret dashes.

The initial source scan found one explicitly named parser temporary,
`Statement ignored`, used only after `FSIM-VHDL-PARSE-124` diagnoses a
misplaced VHDL delay mechanism. Other discarded parse results occur in
delimiter/duplicate/error-recovery paths, while executable-HIR and DesignIR
`static_cast<void>` calls force owning-record construction.

Task 2 turned these rules into the `fsim.v1-legality-audit` catalog gate. It
pins the complete status and gap counts above, rejects duplicate row IDs and
undocumented statuses, requires every non-execute or incomplete row in this
inventory, and checks every path-like evidence link. The same task added the
semantic model ownership validator and cataloged `FSIM-SEM-0001` rejection
before application checking or DesignIR projection. Tasks 5 through 8 classify
each remaining language-specific discard site alongside its owning row.
