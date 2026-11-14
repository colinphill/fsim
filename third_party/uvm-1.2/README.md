<!-- SPDX-License-Identifier: Apache-2.0 -->
# Accellera UVM 1.2 source snapshot

This directory vendors the unmodified Accellera UVM 1.2 reference
implementation:

- upstream archive: `https://www.accellera.org/images/downloads/standards/uvm/uvm-1.2.tar.gz`
- archive SHA-256: `502a2e605ce552bfd9767803c7e99a053715b00f7a9c4c511c3fbfddfb30157c`
- archive size: 2,463,986 bytes
- release identity: `accellera-2014-06` (archive release; no Git commit is asserted)
- license: Apache License 2.0 (`kit/LICENSE.txt`, `kit/NOTICE.txt`)

`kit/` is the archive's `uvm-1.2/` root, extracted byte-for-byte (960 files,
canonical path-and-content tree SHA-256
`badb7104548cabd934c6ca95dd126a3b6ee3c71ff6974acffdd4e63d2bfd1f49`; the
digest is the one frozen in `cmake/FsimUvmSources.cmake`). Nothing under
`kit/` is edited, reformatted, or relicensed. `SHA256SUMS` records every
file under `kit/`; this `README.md` and `SHA256SUMS` are fsim's own files and
sit beside `kit/` so that the tree identity stays that of the archive.

The governed entry points are `kit/src/uvm_pkg.sv`, `kit/src/uvm_macros.svh`,
`kit/src/dpi/uvm_dpi.cc`, `kit/LICENSE.txt`, `kit/NOTICE.txt`, and
`kit/README.txt`. See `docs/uvm-source-provenance.md`.
