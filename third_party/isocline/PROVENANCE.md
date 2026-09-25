<!-- SPDX-License-Identifier: Apache-2.0 -->
# Isocline provenance

This directory vendors the terminal editor Isocline from the official
upstream repository:

- Repository: <https://github.com/daanx/isocline>
- Release: `v1.1.0`
- Annotated tag object: `b4f1796627e75fc765cc26ec8091c683ef4d7417`
- Peeled source commit: `d55a58139badbe83d61c5d89954fa5bddcabe6d7`
- Upstream source tree: `6cca0603efa59ef87c697b6437352022024e6007`
- Upstream license: MIT, retained as [`LICENSE`](LICENSE)

The vendored files are the upstream public header in `include/` and the
implementation sources and headers in `src/`. The only bundled source with a
different license notice is `src/wcwidth.c`: its original Markus Kuhn notice
grants use, copying, modification, and distribution for any purpose without a
fee and disclaims warranties. That complete notice remains in the source file.
Both licenses permit use in closed-source products when their notices are
retained. The included implementation has no third-party library dependency or
submodule. It uses the C runtime and the host terminal APIs (`termios`, `select`,
and related POSIX interfaces, or Win32 console APIs).

Only `src/isocline.c` is compiled. It is upstream's amalgamated translation
unit and includes the implementation sources from `src/`; do not compile the
other `.c` files as separate translation units. The upstream public include
directory is `include/`, and the amalgamation needs `src/` on its private
include path.

## Local patches

The upstream snapshot is patched to meet the fsim Tcl console contract:

- `include/isocline.h`, `src/env.h`, and `src/isocline.c` add a per-read
  console API with Tcl completeness checks, completion freshness validation,
  owner-thread output pumping, and explicit accepted/cancelled/EOF/interrupted
  status.
- `src/completions.c` implements the declared `ic_completion_input` accessor
  (the selected upstream release declares it in the public header but has no
  definition), and adds a contextual hint channel independent of candidates.
- `src/completions.h`, `src/completions.c`, `src/editline.c`, and
  `src/editline_completion.c` carry contextual hints, reject stale completion
  sets immediately before use, poll the output/interrupt callback while the
  completion menu is open, and redraw the saved edit buffer and cursor after
  output. Output control bytes are escaped before terminal display.
- `src/tty.h`, `src/tty.c`, and `src/tty_esc.c` recognize bracketed paste,
  retain at most 4 MiB per paste, and return the paste as one editor event.
- `src/editline.c` normalizes pasted CRLF/CR line endings and inserts the paste
  as one undo step without treating its embedded newlines as Enter. Terminal
  bracketed-paste mode is enabled for the editor call and disabled on return.

The C++ fsim adapter lives in `src/app/tcl_console.*`; it translates the
terminal-neutral completion service's byte ranges/spans, applies history
configuration, redirects output generated during editing through the
owner-thread redraw hook, and disables the editor for redirected streams.

## Build boundary

Compile/link the C editor source and C++ adapter only for Tcl-enabled builds.
Tcl-disabled builds must not compile either file or link the editor. The C
amalgamation has upstream conversion, shadow, and unused-static-helper
warnings under fsim's strict warning set. On GCC/Clang, compile this source
with the source-specific suppressions `-Wno-unused-function`,
`-Wno-conversion`, and `-Wno-shadow`; do not relax warnings for fsim C++ or
other targets. On MSVC, use `/W0` for this vendor target without changing
fsim's own warning policy.
