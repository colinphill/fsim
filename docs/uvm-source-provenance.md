<!-- SPDX-License-Identifier: Apache-2.0 -->
# Governed external UVM sources

Fsim does not vendor or patch the UVM reference implementations. The Batch 159
source harness materializes the two reviewed Accellera archives only in an
isolated build/work directory, validates the archive before extraction, and
then validates every extracted file through a canonical path-and-content tree
digest. Generated manifests record the release identity, archive, source root,
complete tree identity, and exact package, macro, DPI, license, notice, and
release-note entry points.

| Harness ID | Standard/version | Immutable upstream identity | Official archive | Archive SHA-256 | Extracted identity |
|---|---|---|---|---|---|
| `uvm-1.2` | Accellera UVM 1.2, release `accellera-2014-06` | `archive-release`; no Git commit is asserted | `https://www.accellera.org/images/downloads/standards/uvm/uvm-1.2.tar.gz` | `502a2e605ce552bfd9767803c7e99a053715b00f7a9c4c511c3fbfddfb30157c` | 960 files, `badb7104548cabd934c6ca95dd126a3b6ee3c71ff6974acffdd4e63d2bfd1f49` |
| `uvm-2020.3.1` | IEEE 1800.2-2020 kit version 2020.3.1 | release tag/commit `78c06547a2a0a29b3dc9dcafae62b75b2ff61544` | `https://www.accellera.org/images/downloads/standards/uvm/UVM-1800.2-2020.3.1.tar.gz` | `0d6a2ca5811c787e5aa1e945abaaaa5d5c295148d5e704c9fa910d7b288cbcf7` | 326 files, `0d0c409af4ba5984df5a3e7d4730289183fa5b714b019b712c9f01e3f769f980` |

Both kits carry their upstream Apache-2.0 `LICENSE.txt` and `NOTICE.txt`.
The governed entry points are `src/uvm_pkg.sv`, `src/uvm_macros.svh`,
`src/dpi/uvm_dpi.cc`, the license, the notice, and the release README. Their
individual digests are frozen in `cmake/FsimUvmSources.cmake`; the full-tree
digest prevents an unreviewed change elsewhere in either distribution.

Normal fsim configuration uses `FSIM_UVM_SOURCE_MODE=OFF` and performs no
network access. `FETCH` downloads the two exact official archives into
`FSIM_UVM_WORK_ROOT`; `ARCHIVE` consumes exact predownloaded files named by
`FSIM_UVM_1_2_ARCHIVE` and `FSIM_UVM_2020_3_1_ARCHIVE`. All modes reject an
authored-source work root; an ignored configured binary tree remains valid. A
direct materialization is also available:

```sh
cmake -DFSIM_SOURCE_DIR="$PWD" \
  -DFSIM_BINARY_DIR="$PWD/build" \
  -DFSIM_UVM_WORK_ROOT="$PWD/build/uvm-sources" \
  -P cmake/MaterializeUvmSources.cmake
```

The downloaded archives, extracted sources, and generated manifests are build
products and remain outside fsim's authored source, license, and release
inventories.

## Batch 160-161 exact UVM execution resource baseline

The final package-only analysis peaks at approximately 1.69 GiB for UVM 1.2
and 1.94 GiB for UVM 2020-3.1 under a 3-GiB address-space ceiling. With the
Batch 161 exact sequence and register environments included, direct source
runs peak at 4,273,188 KiB for UVM 1.2 and 4,775,952 KiB for UVM 2020-3.1
under 6-GiB ceilings.

Portable compile peaks at 3,308,520 KiB for UVM 1.2 and 3,672,868 KiB for UVM
2020-3.1 under 5/6-GiB ceilings. Two-root O0/O2 phase/TLM design publication
peaks at 3,198,008/3,198,256 KiB for UVM 1.2 and 3,441,912/3,441,688 KiB for
UVM 2020-3.1 under 5/5.5-GiB ceilings. Every interpreter, compiled O0/O2,
debug, VCD/FST, cold/warm cache, and relocated-artifact execution in the Batch
160 baseline remains below 1,000,000 KiB under a 3-GiB ceiling. The expanded
Batch 161 final matrices pass in 467.47 seconds for UVM 1.2 and 566.45 seconds
for UVM 2020-3.1, 2/2 in 1,033.92 seconds. Their sequence/register probes reject
one-record-short integrated
checkpoint limits before partial capture. All fourteen current traces are
17,833 bytes with SHA-256
`95caf4dbb04fa7f1b1397df9b40e03a1fdbc19b90c6387221a8b59635ebd5eff`.
These measurements are local evidence from the exact governed sources and are
ceilings/baselines, not a general host-memory guarantee.

## Batch 162 Change 13 core-smoke evidence

The project-owned core smoke probe is compiled in the same source set as each
unmodified governed package. Direct UVM 1.2 passes in 1:50.06 at 4,299,260 KiB
maximum RSS; direct UVM 2020-3.1 passes in 2:10.02 at 4,861,476 KiB. Portable
compile passes in 2:29.65 at 3,365,800 KiB and 3:17.21 at 3,735,852 KiB.
Object-only O2 elaboration passes in 1:38.54 at 3,239,364 KiB and 2:00.99 at
3,746,852 KiB. Loaded-design interpreter execution passes in 1.50 seconds at
936,660 KiB and 1.69 seconds at 1,041,144 KiB. Every stage reports zero swaps.

The four retained direct and portable VCDs are each 28,345 bytes with SHA-256
`62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
Logs and GNU time records use the
`build/llvm22-ninja-debug-uvm/batch162-change13-*` prefix; the exact object,
design, and trace work roots remain under
`build/llvm22-ninja-debug-uvm/uvm-core-smoke-change13/`.

## Batch 162 Change 14 flow-smoke matrix

The governed serial runner checks the retained project source and standard
flow profiles before every stage. UVM 1.2 passes all ten direct, portable,
interpreter, LLVM O0/O2, cold/warm-cache, and debug stages in 7:53.85 at
4,299,156 KiB maximum RSS. UVM 2020-3.1 passes the same ten stages in 9:46.97
at 4,861,116 KiB. The combined 20/20 matrix completes in 17:40.82 with zero
swaps, and each retained stage log contains ten exact flow-smoke transcripts.

All fourteen matrix VCD/FST files are 28,345 bytes with SHA-256
`62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
The retained CTest, stage-transcript, and GNU time evidence uses the
`build/llvm22-ninja-debug-uvm/batch162-change14-exact-*` prefix.

## Batch 162 Change 15 register-smoke matrix

The exact runner validates retained project register roles and stable standard
field/map/memory profiles before every stage, then executes register hierarchy,
little/big endian and byte-enable behavior, adapter/predictor/frontdoor,
mixed-VPI/VHPI backdoor, standard sequence, callback/coverage, DPI callback,
negative rollback, relocation, checkpoint, and retained-cap assertions. UVM
1.2 passes all ten stages in 7:59.08 at 4,299,536 KiB maximum RSS; UVM
2020-3.1 passes all ten stages in 9:55.15 at 4,860,764 KiB. The combined 20/20
matrix completes in 17:54.23 with zero swaps, and each retained stage log
contains ten exact register-smoke transcripts.

All fourteen matrix VCD/FST files are 28,345 bytes with SHA-256
`62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
The retained CTest and GNU time evidence uses the
`build/llvm22-ninja-debug-uvm/batch162-change15-exact-*` prefix; the focused
direct-source records use `batch162-change15-direct-*`.

## Batch 162 Change 16 capped platform matrix

Each exact-stage child installs a 6 GiB POSIX or Windows process-memory ceiling
before reading governed source. The platform-neutral CMake runner limits every
stage to 1,200 seconds and CTest limits each serial release matrix to 7,200
seconds. The x64 C ABI contract freezes snapshot, record, activity, and host
table sizes/offsets plus the Windows `__cdecl` callback type.

UVM 1.2 passes all ten bounded stages in 7:55.05 at 4,299,868 KiB maximum RSS;
UVM 2020-3.1 passes all ten in 9:55.48 at 4,861,176 KiB. The combined 20/20
matrix completes in 17:50.53 with zero swaps and twenty exact platform-contract
markers. Focused capped direct probes pass in 1:51.12/2:11.75 at
4,299,428/4,861,616 KiB with zero swaps. All fourteen matrix traces remain
28,345 bytes with SHA-256
`62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
Retained records use the `batch162-change16-capped-*` prefix under
`build/llvm22-ninja-debug-uvm/`.

## Batch 162 Change 17 supported-boundary inventory

The authoritative `tests/feature_matrix/uvm_conformance_inventory.tsv` record
contains 18 supported rows and selects exactly 17 families for either release.
Every family names positive, negative, and execution owners. The post-load
application verifier rejects missing evidence, duplicate entries, unknown
release selectors, changed counts, or any missing retained class. It requires
53 governed classes for UVM 1.2, 56 for UVM 2020-3.1, and all 27 project-owned
classes. The fast contract also rejects any failure-waiver entry. No supported
mismatch was observed, and no in-scope implementation gap remained after the
complete inventory ran.

Focused direct inventory probes pass in 1:52.41 at 4,299,032 KiB maximum RSS
for UVM 1.2 and 2:09.16 at 4,860,876 KiB for UVM 2020-3.1. The serial runner
passes all ten direct/object/design/interpreter, LLVM O0/O2, cold/warm-cache,
debug, relocation, replay, and trace stages in 8:03.14 at 4,299,292 KiB and
9:51.85 at 4,861,336 KiB: 20/20 stages in 17:54.99 with zero swaps. Each
retained log contains ten exact conformance-inventory markers. All fourteen
VCD/FST files are 28,345 bytes with SHA-256
`62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
Retained records use the `batch162-change17-direct-*` and
`batch162-change17-exact-*` prefixes under
`build/llvm22-ninja-debug-uvm/`.

## Batch 162 Change 18 release-closure audit

The authoritative `tests/feature_matrix/uvm_release_closure.tsv` record has 21
rows freezing both releases' compatibility switches, retained class/family and
stage counts, memory/trace baselines, artifact/cache provenance, and zero
unresolved supported gaps. Its post-load verifier and registered aggregate
audit also freeze the 82 UVM diagnostic codes, the complete 2,071-code catalog,
826 bounded C/C++ sources, 937 SPDX-owned files, 313 authored test/control
files, and reviewed bounded complexity for the new closure paths.

Focused direct closure probes pass in 1:48.68 at 4,299,044 KiB maximum RSS for
UVM 1.2 and 2:24.74 at 4,860,820 KiB for UVM 2020-3.1. The serial runner passes
all ten stages in 8:28.42 at 4,299,288 KiB and 11:14.87 at 4,860,288 KiB:
20/20 stages in 19:43.29 with zero swaps. Each retained log contains ten exact
closure-audit markers. All fourteen VCD/FST files remain 28,345 bytes with
SHA-256
`62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
Retained records use the `batch162-change18-direct-*` and
`batch162-change18-exact-*` prefixes under
`build/llvm22-ninja-debug-uvm/`.

## Batch 162 Change 19 public contract

The installed [producer-independent tutorial](uvm-tutorial.md), public UVM
guide, architecture/language descriptions, diagnostic and evidence matrices,
resource/provenance records, and release audits are checked as one 15-document
contract. The synchronized Batch 165 release baseline contains 1,294 executable
rows and 5,176 evidence cells across 617 exact paths. The documentation, installed-
public, portability, inventory, and release slice passes 32/32; the staged
Unicode install contains the guide, tutorial, and closure audit.

## Batch 162 Change 20 final matrix

The final governed tree is current after the fresh non-governed Debug/Release
builds and regressions. UVM 1.2 passes all ten exact stages in 8:18.93 and UVM
2020-3.1 passes all ten in 10:20.25, 2/2 in 18:39.19 with peak RSS 4,860,744
KiB and zero swaps. All fourteen regenerated VCD/FST files remain 28,345 bytes
with SHA-256
`62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
The retained outer record is
`build/llvm22-ninja-debug-uvm/batch162-change20-exact-uvm-matrix.log`; stage
artifacts remain under `uvm-phase-tlm-example/`.
