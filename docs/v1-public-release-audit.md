<!-- SPDX-License-Identifier: Apache-2.0 -->
# Final public and installed v1 release audit

This is the Batch 130 Task 6 audit of installed commands, public headers and
libraries, CLI, C API, SystemC ABI/facade, Tcl, runtime files, Unicode paths,
environment behavior, callbacks, diagnostics, and exit status.

## Public contract

The installed v1 surface is:

- `fsim`, `fsim-vhdl`, `fsim-sv`, `fsim-elab`, and `fsim-run` commands;
- `fsim/api.h`, `fsim/systemc.hpp`, `fsim/systemc_abi.h`,
  `fsim/version.hpp`, the `<systemc>` compatibility header, and the complete
  `fsim/systemc/` facade header tree;
- the `fsim_api` shared library and `fsim_systemc_support` library;
- the bundled Tcl runtime and license when selected; and
- project documentation, the Apache-2.0 license, README, and reviewed IEEE
  package tree.

| Review ID | Surface | Required check |
|---|---|---|
| `B130-T6-INSTALL` | Staged install layout and relocation | A Unicode-prefix install contains every command/header/library/document/package artifact and installed public headers are byte-identical to their reviewed sources |
| `B130-T6-CLI` | Commands, aliases, direct sources, project mode, diagnostics, paths, and exit status | Installed version/help behavior is exact; success, user error, invalid arguments, and internal containment retain statuses 0/1/2/3 |
| `B130-T6-API` | C session/handle/object/value/callback/debug lifecycle | API version/layout, misuse, Unicode project paths, callback re-entry/exception containment, stepping, forcing, files, and diagnostics remain executable |
| `B130-T6-SYSTEMC` | Strict-C ABI and public C++ facade | Installed headers retain ABI version/layout, compile ownership, datatype/port/export/interface behavior, and no C++ exception crosses the C boundary |
| `B130-T6-TCL-RUNTIME` | Tcl commands/scripts/results, bundled relocation, runtime file I/O, environment, debugger, and VCD | UTF-8/native paths, unset-versus-empty environment values, binary modes, relocatable Tcl scripts, and callback/error lifetimes remain test-owned |

The staged-install test performs no network access. It uses the already built
tree, installs beneath a fresh Unicode work directory, verifies the exact
layout and header bytes, runs the installed main and alias commands, and checks
the invalid-option exit status. Compilation of those same public C, strict-C,
and C++ facade headers remains owned by the normal warning-as-error build and
its dedicated header/ABI CTests.

## Closure evidence

The eight-worker Debug build is warning-clean. The public/static and staged
install audits, C and C++ API/ABI headers, complete CLI application, scoped
locals, SystemVerilog files, Tcl application and fetched-runtime relocation,
and C API passed 11/11 in 14.05 seconds. The staged Unicode-prefix install
completed in 0.61 seconds and verified exact command, library, header,
documentation, and IEEE-package layout plus installed version/help/error
behavior.

Task 6 performs no sanitizer, Release, full regression, commit, push, or
GitHub Actions inspection. Those accumulated gates remain Task 10 work.
