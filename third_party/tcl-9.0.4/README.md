<!-- SPDX-License-Identifier: Apache-2.0 -->
# Tcl 9.0.4 patches

fsim builds the official Tcl 9.0.4 source release when no compatible Tcl
development package is installed. `cmake/FsimTcl.cmake` downloads the archive
and checks its SHA-256. On Windows it then runs `cmake/PatchTcl.cmake`, which
applies the patches listed in `PATCHES.txt`. Tcl's Unix build never compiles
the patched `win/` sources, so other hosts build the release unchanged.

`PATCHES.txt` records each patch file's SHA-256 and the SHA-256 of every file
it changes, before and after. `cmake/PatchTcl.cmake` checks the inputs, applies
the patch with `patch -p1 --fuzz=0`, and checks the outputs, so the build uses
exactly the reviewed tree. If the step runs again on an already patched tree,
it accepts that tree when every output digest matches.

## tcl-windows-long-paths

Windows rejects ordinary paths beyond `MAX_PATH` (260 characters) unless both
the process and the host opt in. Tcl 9.0.4 adds the `\\?\` extended-length
prefix only to absolute paths longer than 260 characters. That leaves several
of its own file commands unable to use the long paths the rest of fsim
supports:

- `CreateDirectoryW` fails beyond 248 characters. `file mkdir` creates each
  parent in turn, so every deeper path fails in the 248 to 260 character range.
- Relative paths are never extended, whatever their full length.
- `glob` builds its search pattern from the ordinary spelling. A long
  directory then reports no such path, which `glob` reads as no matches.
- Recursive `file copy` and `file delete` walk the ordinary spelling. A long
  tree reports no such path, which `file delete` treats as success, leaving
  the tree in place.
- `pwd`, the cached native working directory, and the device number used by
  `file stat` read into `MAX_PATH` buffers.
- Replacing an existing file with `file rename -force` names a backup with
  `GetTempFileNameW`, which cannot work beyond `MAX_PATH`.

The patch extends native paths from 248 characters, resolves long relative
paths first, and extends glob patterns and the roots of recursive copies and
deletes. It sizes the working-directory and device-number buffers to the path,
and replaces a long rename target directly with `MoveFileExW`. Errors from an
extended path are reported in the ordinary forward-slash spelling. Shorter
paths behave exactly as before.

Tcl's `fCmd`, `winFCmd`, `winFile`, `fileName`, `fileSystem`, and `cmdAH`
tests give identical results before and after the patch: 17,937 tests, 17,347
passed, 590 skipped, 0 failed. Changing to a working directory beyond
`MAX_PATH` still requires Windows long-path support, which fsim's manifest
requests, and `exec` still searches for programs with `MAX_PATH` buffers.
