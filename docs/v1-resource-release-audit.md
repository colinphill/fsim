<!-- SPDX-License-Identifier: Apache-2.0 -->
# Final v1 platform and resource audit

This is the Batch 130 Task 8 static reconciliation of the bounded hosted
build/test contract. It inspects checked-in configuration and executable phase
ownership; it does not query or claim a current GitHub Actions result.

| Review ID | Frozen release contract | Required ownership |
|---|---|---|
| `B130-T8-MATRIX` | 12 hosted configurations | Linux GCC Debug/Release, Linux exact LLVM 22.1.8 Debug/Release, sanitizer, fuzz, Windows MSVC Debug/Release, and Windows MSVC/clang-cl plus exact LLVM 22.1.8 Debug/Release are configured |
| `B130-T8-BUILD` | Six hosted build steps at four workers; local Ninja link/archive pool at eight | Hosted memory pressure remains bounded while all local builds retain the approved minimum eight-way concurrency |
| `B130-T8-MEMORY` | Compact non-MSVC Debug objects and an 8 MiB MSVC-compatible test stack | GCC Debug uses `-Og` and compressed debug information; every configured C/C++ test host receives the common stack policy |
| `B130-T8-TESTS` | 60/120/600/900/1200-second bounded test classes | Scoped locals, API, Windows application, serialized SystemC matrix, and the container differential retain explicit diagnostic timeouts; hosted jobs retain 20/45/70-minute bounds |
| `B130-T8-TRACE` | Scoped-local and SystemC phase traces | A future timeout identifies build, parse, interpreter, compiled, warm-cache, integration, or scheduling phase rather than appearing as an opaque hang |
| `B130-T8-PLATFORM` | 14 explicit platform boundary files and 20 exact portability rows | UTF-8/native paths, compiler/runtime ABI, DLL/cache behavior, API/ABI, Debug/Release, LLVM/cache/debug/VCD, and bounded resources retain checked owners |

## Closure evidence

The eight-worker Debug build required no compilation. The final resource gate,
portability audit/corpus, MSVC Debug/Release, Windows LLVM, SystemC/tool/resource
portability contracts, SystemC application matrix, and scoped-local guard
passed 11/11 in 46.30 seconds. SystemC completed in 45.30 seconds, scoped
locals in 0.84 seconds, and the composed resource audit in 0.07 seconds.

The Task 10 hosted inspection remains mandatory after the final checkpoint is
pushed. Task 8 performs no hosted execution, sanitizer, Release, full
regression, commit, push, or GitHub Actions inspection.

## V2 Batch 162 governed UVM resource addendum

The supported two-release closure matrix applies a 6 GiB child-process address-
space ceiling, 1,200-second stage limits, and a 7,200-second serial release
limit. Its final Change 18 evidence passes UVM 1.2 ten stages in 8:28.42 at
4,299,288 KiB maximum RSS and UVM 2020-3.1 ten stages in 11:14.87 at 4,860,288
KiB: 20/20 in 19:43.29 with zero swaps. All fourteen retained VCD/FST files are
28,345 bytes with SHA-256
`62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
These are retained local baselines, not a promise that arbitrary UVM projects
use the same memory. CI should preserve exact logs and avoid concurrent full
release matrices unless the host can supply their combined resident memory.

Change 20's fresh clean-first Debug/Release builds complete 733 steps in
9:36.84/8:49.59 at 4,070,136/2,506,320 KiB maximum RSS. Their 122/122 complete
regressions pass in 6:24.90/5:26.29 at 3,777,748/3,783,548 KiB. The final exact
two-release UVM matrix passes in 18:39.19 at 4,860,744 KiB. Every measured run
reports zero swaps.
