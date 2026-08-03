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
