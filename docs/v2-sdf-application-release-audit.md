<!-- SPDX-License-Identifier: Apache-2.0 -->
# Batch 169 SDF application release audit

This audit freezes the completed documentation and evidence boundary for
Verilog/SystemVerilog SDF timing application. Batch 170, not Batch 169, owns
VHDL/VITAL timing and any timing target that crosses a VHDL boundary.

## Reviewed scope

- `B169-C19-LEDGER`: all seventeen Changes 2-18 rows in
  `tests/feature_matrix/sdf_application_inventory.tsv` are `preserved`; its
  SHA-256 is
  `47e7f5b95aae9f0e3df5cb4fcb4939255e1803f9c920081a47198e21b75f2754`.
- `B169-C19-PROFILES`: SDF 2.1/3.0/4.0 is applied to Verilog-1995/2001/
  2001-noconfig/2005 and SystemVerilog-2005/2009/2012/2017 without profile
  widening.
- `B169-C19-PUBLIC`: CLI, Tcl, installed native C and source C++ controls;
  object/design/library/cache/checkpoint persistence; and debugger, callback,
  internal-trace, VPI and VCD observation have executable owners.
- `B169-C19-NEGATIVE`: missing, mismatch, conflict, overflow and resource
  failures are cataloged and transactional.
- `B169-C19-DEFERRED`: VHDL/VITAL application remains exclusively Batch 170.

## Executable closure

`fsim.sdf-application-closure` runs seven serial stages: corpus, timing,
publication, artifact, public, project and contracts. It requires time
advancement, exact timing and violation results, interpreter/LLVM,
optimized/debug, project/non-project, object/design/library, cold/warm/
relocated cache, checkpoint replay, debugger/callback/trace/VPI/VCD,
Linux/Windows contract, negative-family and clean-exit tokens independently of
child exit status.

The Change 18 Debug run passed in 33.25 seconds. It retained `console.log`,
seven verbose stage logs totaling 30,437 bytes and a seven-row PASS
`result.tsv` under `tests/sdf-application-closure-evidence/` in the selected
build tree. The indexed graph contained 32,230 nodes and 148,489 edges.

## Resource and performance review

- Runtime requests precompute exact ticks and stable target identities; event
  execution performs no SDF parse, rational conversion or hierarchy lookup.
- Observation records use caller-configured fixed capacity; disabled
  observation performs no per-event storage or lookup.
- Control report size, archive bytes/records/strings and decode work are
  explicitly bounded and reject before publication.
- The corpus requires a positive time advance and clean finish, so an early
  exit or zero-status child without the transcript cannot satisfy closure.
- Changes 1-19 use focused exact-LLVM Debug validation. Release builds and
  tests occur only in final Change 20 checks.
- Formatting is limited to changed implementation sources. Semantic header
  edits are not reformatted merely for style, avoiding rebuild-only churn.

## Catalog, source and license inventory

The synchronized static gates cover exactly 2,389 emitted production
diagnostic codes and 974 authored C/C++ sources under the 2,500-line hard
limit with a 2,000-line refactor target. The Batch 169 authored artifact set
contains 1,139 files, each with an Apache-2.0
SPDX notice. The repository `LICENSE` remains the reviewed Apache license.

## Final Change 20 evidence and handoff

Fresh clean-first exact-LLVM 22.1.8 Debug and Release builds pass all 874 steps
with eight workers in 12:18.95 and 10:03.81, at 5,141,632 and 2,255,432 KiB
peak RSS and zero swaps. Final complete non-sanitized regressions pass 182/182
in 8:45.20 and 7:37.31, at 3,780,536 and 3,788,200 KiB peak RSS and zero swaps.
The final SDF application closures pass in 41.99 and 37.91 seconds. Eight
primary retained build/test logs and metric records contain 399,038 bytes,
report exit zero and contain no compiler warning/error marker.

Every source, catalog, inventory, installed-public, relocation, differential,
resource, platform and release gate participates in the all-green final
transcripts. The refreshed graph contains 32,232 nodes and 148,494 edges.
Batch 169 ran no sanitizer and no hosted-CI inspection because it is neither
boundary. The accumulated implementation may now be committed and pushed with
the exact Batch 170 restart checkpoint.
