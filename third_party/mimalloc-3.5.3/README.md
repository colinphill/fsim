<!-- SPDX-License-Identifier: Apache-2.0 -->
# mimalloc 3.5.3 source archive

This directory vendors the unmodified release archive of
[mimalloc](https://github.com/microsoft/mimalloc) 3.5.3 (tag `v3.5.3`, commit
`d4881d338125e1cb7c47ba4cfb398d6f7c0c8d45`, released on September 16, 2026). It
was downloaded from the
[tag archive URL](https://github.com/microsoft/mimalloc/archive/refs/tags/v3.5.3.tar.gz).
Upstream publishes no checksum for it, so `SOURCE_MANIFEST.txt` records the
retrieved archive's size and SHA-256, the tag and commit, and the extracted
source identity. `mimalloc-3.5.3.spdx.json` records the same component for the
source SBOM. mimalloc is MIT-licensed; `LICENSE` is the upstream file.

`cmake/FsimMimalloc.cmake` verifies the archive, extracts it into the build tree,
and verifies the extracted tree (382 files) before creating the
`fsim_mimalloc` target. It repeats the source check on later configuration runs.
The tree checksum is SHA-256 over sorted lines of
`<file-sha256><two spaces><relative-path>\n`. Configuration and compilation
require no network access or system mimalloc.

mimalloc replaces the C and C++ allocator of the `fsim` executable. Its heap
reuses memory instead of returning it to the operating system and touching
fresh pages again, which removes most of the page faults of compilation,
elaboration and simulation.
- On Linux and other ELF systems, the unmodified `src/static.c` is compiled into
  the executable with `MI_MALLOC_OVERRIDE`, so `malloc`, `free` and the C++
  allocation operators of the executable and its shared libraries resolve to
  mimalloc.
- On Windows, the same file is built as `fsim-mimalloc.dll` with the upstream
  prebuilt redirection module (`bin/mimalloc-redirect.dll` and its import
  library), installed next to `fsim.exe`, as described in the upstream
  `bin/readme.md`. This path is not yet qualified on a Windows host.

`FSIM_MIMALLOC=OFF` keeps the platform allocator. It is off automatically for
sanitizer builds and for builds with `FSIM_ENABLE_ALLOCATION_PROFILING`, which
define their own allocation operators. Upstream source is outside fsim's
authored-source line budgets and warning-as-error policy.
