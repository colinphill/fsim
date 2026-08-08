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

## Batch 160 phase/TLM execution resource baseline

The final package-only analysis peaks at approximately 1.69 GiB for UVM 1.2
and 1.94 GiB for UVM 2020-3.1 under a 3-GiB address-space ceiling. The final
exact phase/TLM direct source runs peak at 4,242,276 KiB for UVM 1.2 and
4,735,592 KiB for UVM 2020-3.1 under 6-GiB ceilings.

Portable compile peaks at 3,308,520 KiB for UVM 1.2 and 3,672,868 KiB for UVM
2020-3.1 under 5/6-GiB ceilings. Two-root O0/O2 phase/TLM design publication
peaks at 3,198,008/3,198,256 KiB for UVM 1.2 and 3,441,912/3,441,688 KiB for
UVM 2020-3.1 under 5/5.5-GiB ceilings. Every interpreter, compiled O0/O2,
debug, VCD/FST, cold/warm cache, and relocated-artifact execution remains below
1,000,000 KiB under a 3-GiB ceiling and produces the exact transcript through
the tick-5 race and cataloged deadlock. These measurements are local evidence
from the exact governed sources and are ceilings/baselines, not a general host-
memory guarantee.
