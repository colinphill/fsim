<!-- SPDX-License-Identifier: Apache-2.0 -->
# Verilog-2005 closure audit

This audit closes Batch 164's governed IEEE 1364-2005 residual-language and
literal-width boundary. SDF belongs to Batch 170; removed TF/ACC interfaces and
informative optional annex services remain post-v2 compatibility work.

## Inventories

The 37-row clause inventory contains 34 supported rows, zero unresolved active
rows, and three explicit deferrals. Every supported row owns parser, analyzer,
elaboration, runtime, diagnostic, and resource paths plus independent positive,
negative, and execution evidence.

The separate 15-row literal-width inventory contains 12 preserved paths, zero
active arbitrary-width obligations, and three explicit physical boundaries:
governed packed materialization, scalar host conversion, and the scalar VPI
integer format. Those boundaries do not redefine Verilog literal legality.

The 46-row release-closure matrix maps the 34 supported clause rows and 12
preserved width paths to 138 witness cells, exactly 23 registered CTests, and 17
direct/interpreter/LLVM/cache/debug/VCD/artifact/relocation/replay/checkpoint/
root/mixed-language stages. It contains no expected-failure, waiver, allowlist,
or suppression path.

The exact SHA-256 identities are:

- clause inventory: `338b64ba883f6243d9f799d31c22f873e871a5977bbb9fc9c45ae3b5ea738c84`;
- literal-width inventory: `a1cef90f58dc984e69e1f6c35247090498553d4f8d10c57b1a1bcd2fc06aee05`;
- release closure: `a80eea635da93dff681c7118cd3339e7b4bb679c83a2cf7137cc3eeccef6f39e`.

## Runtime and resource evidence

The governed typed-boundary witness passes at 196,716 KiB peak RSS with zero
swaps, and the artifact witness passes at 97,372 KiB with zero swaps. The final
23-witness serial closure matrix passes in 94.63 seconds and retains one log per
witness plus a result ledger.

Each transcript owner installs a 6 GiB process address-space ceiling through
POSIX `RLIMIT_AS` or Windows `JOB_OBJECT_LIMIT_PROCESS_MEMORY`. Simulation work
is capped at 1,000 deltas, VCD registration at 64 signals, each stage at 1,200
seconds, and the complete matrix at 7,200 seconds. These bounds govern release
evidence and prevent runaway resource use; they are not Verilog width or
language-legality limits.

The completed width evidence covers 257- and 4,097-bit parsing/folding,
129/137/257-bit execution, exact X/Z formatting and memory words, full-word LLVM
O0/O2 register planes, mixed/public views, 9,600,008-bit VPI descriptor
validation, and object/library/design/checkpoint/cache round trips.

## Publication and persistence evidence

The complete run exposed and repaired three publication defects: duplicate
synthetic process siblings now use stable runtime process identities,
bidirectional switch topology no longer publishes phantom drivers or
transactions, and fixed memories own their canonical VPI Memory/word hierarchy.

Native cache schema 90 keys every packed constant word and state plane. Design
artifact schema 4 preserves complete executable SystemVerilog HIR for Verilog
units. Owning objects, mapped libraries, standalone designs, VPI checkpoints,
relocation, replay, and cold/warm cache paths retain exact literal text, width,
signedness, X/Z state, timing identity, and source-independent execution.

## Synchronized release baseline

After adding this public documentation contract, the live static inventory is
2,228 production diagnostics, 885 bounded C/C++ sources, 1,021 SPDX-owned files,
and 330 authored test/control files. The closure audit composes diagnostic,
source-line, SPDX, v1 conformance, and resource-portability gates without
waivers.

## Passing criteria

The registered clause, closure, documentation, diagnostic, source, license,
installed-public, platform, portability, and release contracts must pass. No
supported row may disappear behind a parser-only claim or expected failure. The
[support guide](verilog-2005.md) and [tutorial](verilog-2005-tutorial.md) are
installed public documentation and must remain synchronized with the machine
inventories.
