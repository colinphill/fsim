<!-- SPDX-License-Identifier: Apache-2.0 -->
# Verilog-2005 support

Fsim implements the governed IEEE 1364-2005 boundary recorded by
[`verilog_gap_inventory.tsv`](../tests/feature_matrix/verilog_gap_inventory.tsv).
Its 34 supported clause rows have independent positive, negative, and execution
witnesses, no active residual row is unresolved, and three boundaries are
explicitly deferred. SDF parsing and backannotation belong to Batch 170;
removed legacy TF/ACC interfaces and informative optional annex services remain
post-v2 compatibility work.

The separate
[`verilog_literal_width_inventory.tsv`](../tests/feature_matrix/verilog_literal_width_inventory.tsv)
closes 12 value paths and records three physical host/resource boundaries. This
is a closed statement for the inventoried Verilog-2005 surface, not a claim that
deprecated PLI or proprietary simulator extensions are emulated.

## Literal-width design rule

Verilog source legality is independent of a host word. Bit-string and based
number tokens retain their complete spelling; explicit widths, signedness,
binary/octal/decimal/hexadecimal digits, and every `X`, `Z`, or `?` digit remain
exact beyond 64 bits. Parsing and constant folding use arbitrary-precision
limbs, and packed runtime values use owning word vectors. Interpreter and LLVM
O0/O2 execution, memory files, formatted output, debugger/VCD/public views,
artifacts, checkpoints, relocation, and native-cache identity preserve every
word and state plane.

Only the following physical boundaries remain:

- allocating a packed value is subject to explicit host-addressability,
  owning-storage, and evaluation-work ceilings;
- an operation that specifically requests a scalar host format performs a
  checked conversion, while packed callers continue to use vector storage; and
- VPI integer values use the 64-bit scalar ABI, while VPI vector descriptors
  use an explicit `uint32_t` width field and carry all host-addressable bits.

A physical-boundary diagnostic means that a requested representation or amount
of work cannot be materialized. It does not make the literal grammatically or
semantically illegal, and no implementation-defined 64-bit or one-megabit
language-width ceiling is permitted.

## Language and execution boundary

The supported surface covers lexical tokens and directives, declarations,
modules and generated hierarchy, parameters and exact range derivation,
expressions and assignments, gates/UDP/switch networks and strengths, scheduler
controls, functions/tasks/system services, memories and memory-file operations,
specify paths and all twelve timing checks, VPI/public integration, multiple
roots, and mixed VHDL/SystemC boundaries. Verilog-only legality remains distinct
from the broader SystemVerilog-2017 surface; using a SystemVerilog-only form in
2005 mode produces its cataloged language diagnostic.

Recursive callables use dynamic frames rather than a fixed recursion depth.
Wide packed processes use full-word interpreter and compiled register planes;
an unavailable native capability falls back per process without narrowing the
value. Deterministic scheduler progress remains bounded by the configured
`max_deltas` work policy.

## Specify timing and VPI

Module-owned specify HIR retains `specparam`, path, edge, condition, polarity,
pulse, notifier, delayed-terminal, and timing-check state. Each normalized path
or timing check has a stable instance-qualified identity suitable for later SDF
application. The common scheduler supplies inertial path routing, cancellation
policy, exact simultaneous-boundary ordering, callbacks, debugger visibility,
and VCD publication. SDF syntax and annotation are deliberately outside this
layer until Batch 170.

Each live simulation publishes generation-qualified VPI roots, modules,
generated scopes, ports, nets, variables, parameters, memories and words,
named events, processes, drivers, and transactions. Duplicate synthetic process
names use stable runtime identities, fixed memories own their canonical VPI
hierarchy, and bidirectional switch topology does not invent phantom drivers.
Packed vector reads and writes retain exact ranges, signedness, strengths, and
four-state planes. See [SystemVerilog VPI support](systemverilog-vpi.md) for the
host ABI and restart contract.

## Artifacts, cache, and governed evidence

Portable `.fsimobj`, mapped `.fsimlib`, and standalone `.fsimdesign` artifacts
retain complete executable SystemVerilog HIR for Verilog units, exact literal
text and packed state, stable timing identities, and checked runtime state.
Checkpoint and native-cache schemas include every packed word and state plane;
incompatible, corrupt, stale, or over-materialized payloads reject before
publication.

The release matrix runs 23 registered witnesses serially through 17 direct,
interpreter, LLVM, cache, debugger, VCD, artifact, relocation, replay,
checkpoint, multiple-root, and mixed-language stages. Its application processes
use a cross-platform 6 GiB address-space ceiling, 1,000-delta work limit,
64-signal VCD capacity, 1,200-second per-stage timeout, and 7,200-second matrix
timeout. These are test-evidence ceilings, not Verilog language limits.

See the [tutorial](verilog-2005-tutorial.md) for ordinary project and portable
artifact flows, [language support](language-support.md) for the surrounding HDL
boundary, [architecture](architecture.md) for ownership and scheduling, and the
[closure audit](verilog-2005-closure-audit.md) for exact counts and digests.
