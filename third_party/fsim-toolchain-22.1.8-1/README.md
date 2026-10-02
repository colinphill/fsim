<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim toolchain 22.1.8-1

This directory records the Windows toolchain fsim is built with and bundles.
It holds no binaries. The toolchain is the
[fsim-22.1.8-1 release](https://github.com/colinphill/fsim-toolchain/releases/tag/fsim-22.1.8-1)
of [fsim-toolchain](https://github.com/colinphill/fsim-toolchain), a fork of
LLVM-MinGW 20260616 that targets only x86-64 Windows with the UCRT: clang,
lld and LLDB for LLVM 22.1.8, built with RTTI and with only the X86 backend.
It is the only supported Windows toolchain.

The release publishes three archives, all recorded in `SOURCE_MANIFEST.txt`
with their URL, size and SHA-256:

- the build toolchain, which CI extracts and builds fsim with;
- the LLVM development overlay (headers, CMake package and `libLLVM-22.dll`
  import library), extracted over the build toolchain so fsim compiles its JIT
  against exactly the `libLLVM-22.dll` the toolchain ships;
- the redistributable toolchain, which is the build toolchain without its
  GPL- and LGPL-licensed build tools.

`SOURCE_MANIFEST.txt` also records the fork commit, the SHA-256 and size of the
runtime DLLs fsim imports, and the digests of `LICENSE` and `NOTICE`.
`fsim-toolchain-22.1.8-1.spdx.json` records the redistributable archive and
those DLLs for the SBOM. `LICENSE` is the archive's `LICENSE.TXT`, byte for
byte.

`cmake/FsimWindowsRuntime.cmake` uses this record in one of two ways:

- With `FSIM_WINDOWS_TOOLCHAIN_REDIST` naming the redistributable archive,
  configuration checks its SHA-256 and that it comes from the same build as
  the compiler, and installation copies the whole toolchain into the install
  prefix beside fsim. The plug-in compiler then defaults to the bundled
  `bin/clang++.exe`, so plug-ins use fsim's compiler and C++ runtime, and
  `bin/lldb.exe` can debug them. CI and the binary archives use this mode.
- Otherwise, installation copies only `libc++.dll`, `libunwind.dll` and, in
  LLVM-enabled builds, `libLLVM-22.dll` from the compiler's directory, and
  fails if one differs from its recorded SHA-256.

Either way, these provenance files are installed into
`share/doc/fsim/third-party/fsim-toolchain-22.1.8-1`.
