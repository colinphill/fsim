<!-- SPDX-License-Identifier: Apache-2.0 -->
# Accellera UVM 2020.3.1 (IEEE 1800.2-2020) source snapshot

This directory vendors the unmodified Accellera IEEE 1800.2-2020 reference
implementation, kit version 2020.3.1:

- upstream archive: `https://www.accellera.org/images/downloads/standards/uvm/UVM-1800.2-2020.3.1.tar.gz`
- archive SHA-256: `0d6a2ca5811c787e5aa1e945abaaaa5d5c295148d5e704c9fa910d7b288cbcf7`
- archive size: 724,207 bytes
- release commit: `78c06547a2a0a29b3dc9dcafae62b75b2ff61544`
- license: Apache License 2.0 (`kit/LICENSE.txt`, `kit/NOTICE.txt`)

`kit/` is the archive's `1800.2-2020.3.1/` root, extracted byte-for-byte (326
files, canonical path-and-content tree SHA-256
`0d0c409af4ba5984df5a3e7d4730289183fa5b714b019b712c9f01e3f769f980`; the
digest is the one frozen in `cmake/FsimUvmSources.cmake`). Nothing under
`kit/` is edited, reformatted, or relicensed. `SHA256SUMS` records every
file under `kit/`; this `README.md` and `SHA256SUMS` are fsim's own files and
sit beside `kit/` so that the tree identity stays that of the archive (the
kit has its own `README.md`).

The governed entry points are `kit/src/uvm_pkg.sv`, `kit/src/uvm_macros.svh`,
`kit/src/dpi/uvm_dpi.cc`, `kit/LICENSE.txt`, `kit/NOTICE.txt`, and
`kit/README.md`. See `docs/uvm-source-provenance.md`.
