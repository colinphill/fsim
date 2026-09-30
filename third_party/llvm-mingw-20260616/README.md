<!-- SPDX-License-Identifier: Apache-2.0 -->
# LLVM-MinGW 20260616 runtime libraries

This directory records the LLVM-MinGW runtime DLLs that fsim's Windows binary
archives carry, so the installed commands run on a host without the toolchain.
It holds no binaries. The DLLs come from the pinned
[LLVM-MinGW 20260616](https://github.com/mstorsjo/llvm-mingw/releases/tag/20260616)
UCRT x86-64 release, the only supported Windows toolchain.

`SOURCE_MANIFEST.txt` records the release archive's URL, size and SHA-256, the
SHA-256 and size of each bundled DLL, and the digests of `LICENSE` and
`NOTICE`. `llvm-mingw-20260616.spdx.json` records the same components for the
SBOM. `LICENSE` is the archive's `LICENSE.TXT`, byte for byte.

`cmake/FsimWindowsRuntime.cmake` installs `libc++.dll` and `libunwind.dll`, and
`libLLVM-22.dll` in LLVM-enabled builds, from the compiler's directory into
`bin`. It installs these provenance files into
`share/doc/fsim/third-party/llvm-mingw-20260616`. Installation fails if a DLL
differs from its recorded SHA-256, so an archive cannot ship runtime files
that this record does not describe.

fsim compiles against the matching MSYS2 LLVM 22.1.8 development files, but the
Windows test lanes load `libLLVM-22.dll` from the toolchain's `bin` directory.
The archive ships that tested DLL. The installed-archive audit runs the packaged
commands with `PATH` unset, which proves the archive needs nothing else.
