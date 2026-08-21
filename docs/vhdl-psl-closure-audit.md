<!-- SPDX-License-Identifier: Apache-2.0 -->
# VHDL-2008 and embedded PSL closure audit

This audit closes Batch 163's governed digital VHDL-2008 and embedded-PSL
boundary. It does not include VHDL-AMS, SDF backannotation, full standalone
IEEE 1850, or post-2008/vendor extensions.

## Inventories

The 33-row clause inventory contains 29 supported active rows, zero unresolved
active rows, and four explicit deferrals. Every supported row owns parser,
analyzer, elaboration, runtime, diagnostic, and resource paths plus independent
positive, negative, and execution witnesses: 87 witness fields total.

The 44-row release-closure matrix freezes 17 direct/interpreter/LLVM/cache/
debug/trace/artifact/relocation/replay/root/mixed stages, malformed input,
immutable simultaneous-clock snapshots, abort cancellation, bounded
nonconvergence, transactional resource failures, POSIX/Windows enforcement,
installed-public execution, provenance, current inventory counts, and reviewed
complexity.

The exact SHA-256 identities are:

- gap inventory: `0a60ca24775a1e5e7282059b282557d36656e1f926d4207f1f3df48f4c8b891b`;
- release closure: `a7f91f02278161c4ba2451efe3a9ec2914babc9589e9996b067a6e77167eabd4`.

## Runtime and resource evidence

The direct governed application completes all 17 stages in 1.71 seconds at
195,272 KiB peak RSS with zero swaps. The final closure/platform/public slice
passes 13/13 in 3.81 seconds at 195,064 KiB with zero swaps. A 6 GiB process
address-space ceiling, 1,000-delta limit, 64-signal trace limit, and
1,200-second CTest timeout make work failure explicit before over-20-GiB
behavior is possible.

The PSL runtime independently tests monitor, active/lifetime-attempt, history,
evaluation, temporal-step, and storage ceilings. Each resource error has a
typed identity and rejects transactionally. Malformed syntax/typing has exact
cataloged locations, simultaneous root clocks share an immutable value map,
abort outcomes are deterministic, and scheduler nonconvergence stops at the
configured delta boundary.

## Source, platform, and ABI evidence

The reviewed runner functions have maximum cognitive complexity 20, loop depth
1, no scan-in-loop flag, and no recursion. The application limit uses POSIX
`RLIMIT_AS` or Windows `JOB_OBJECT_LIMIT_PROCESS_MEMORY`. The installed-public
contract executes staged `fsim-vhdl --help` in a Unicode installation root.

The strict C ABI retains the old runtime-table tail at offset 560, appends the
VHDL driver-force callbacks at offsets 568, 576, and 584, and places the
exact-width signal callback at offset 592; the current table size is 600 bytes.
Object/library/design payloads and portable VHPI checkpoints
retain checksummed artifact and cache identity rather than host addresses.

## Passing criteria

The registered inventory, closure, documentation, diagnostic, source, license,
installed-public, Windows, SystemC, tool, and resource contracts must all pass.
No supported row can be marked as an expected failure, suppression, or
exception. The [support guide](vhdl-psl.md) and
[tutorial](vhdl-psl-tutorial.md) are installed public documentation and must
remain synchronized with these machine records.
