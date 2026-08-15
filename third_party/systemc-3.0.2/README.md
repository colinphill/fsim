<!-- SPDX-License-Identifier: Apache-2.0 -->
# Accellera SystemC 3.0.2 source archive

This directory vendors the unmodified source archive published from the
official Accellera SystemC `3.0.2` release tag. The Accellera SystemC download
page links to this exact GitHub tag archive.

`SOURCE_MANIFEST.txt` records the official URL, release tag and commit, archive
size and digest, deterministic extracted-tree identity, required license and
notice identities, and the SPDX package URL. `systemc-3.0.2.spdx.json` records
the corresponding SBOM component. The adjacent `LICENSE` and `NOTICE` files
are byte-for-byte copies from the governed archive.

fsim never patches this archive in place. `FsimSystemCAccellera.cmake` validates
the archive before extraction, extracts only into an isolated build-tree root,
validates all 4,456 files, and writes a local materialization manifest. An
alternate archive is accepted only when every governed identity is identical,
which permits fully offline configuration and rejects corrupt, wrong-version,
or incomplete source before any SystemC target is configured.
