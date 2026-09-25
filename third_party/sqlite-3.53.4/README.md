<!-- SPDX-License-Identifier: Apache-2.0 -->
# SQLite 3.53.4 source archive

This directory vendors the unmodified amalgamation archive for SQLite 3.53.4,
released on July 24, 2026. It was downloaded from the
[official source URL](https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip)
and checked against the SHA3-256 checksum on the
[SQLite download page](https://www.sqlite.org/download.html).

`SOURCE_MANIFEST.txt` records the source ID, release date, archive size, SHA-256
and upstream SHA3-256 checksums, and the extracted source identity.
`sqlite-3.53.4.spdx.json` records the same component for the source SBOM. SQLite
is [public domain](https://www.sqlite.org/copyright.html); `LICENSE` reproduces
the notice and blessing from `sqlite3.h` with the C comment markers removed.

`cmake/FsimSqlite.cmake` verifies the archive, extracts it into the build tree,
and verifies all four extracted files before creating the `fsim_sqlite` target.
It repeats the source check on later configuration runs. The tree checksum is
SHA-256 over sorted lines of `<file-sha256><two spaces><relative-path>\n`.
Configuration and compilation require no network access or system SQLite.

Only `sqlite3.c` is compiled, as a static library with position-independent code
and hidden C symbol visibility. Its header directory is a system include for
internal consumers. fsim uses SQLite's serialized threading mode
(`SQLITE_THREADSAFE=1`) and omits dynamic extension loading
(`SQLITE_OMIT_LOAD_EXTENSION=1`), as described in the
[upstream compile options](https://www.sqlite.org/compile.html). fsim does not
build or install the SQLite shell, expose a SQLite package target, or install
its development headers. Upstream source is outside fsim's authored-source
line budgets and warning-as-error policy.
