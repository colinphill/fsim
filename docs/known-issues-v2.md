<!-- SPDX-License-Identifier: Apache-2.0 -->
# v2.0 release-candidate known issues and exclusions

This list separates candidate limitations from defects. A final v2.0 claim may
remove a row only with Batch 177 evidence.

- No Batch 176 Release build, archive, install smoke, sanitizer or hosted-CI
  result exists. Batch 177 owns all of them, including every Windows result.
- Candidate ZIPs are explicitly unsigned. Batch 177 records the final signature
  or preserves an explicit unsigned disposition; no signature is inferred.
- Windows packages target pinned LLVM-MinGW 20260616 UCRT. Native MSVC and
  clang-cl retain checked source/command portability contracts but are retired
  as package targets and have no current binary-package claim.
- Exact LLVM 22.1.8 is optional. Current-only native cache or plug-in payloads
  reject incompatible compiler, target, ABI, schema or content identities and
  must be regenerated from portable inputs.
- Windows pkg-config execution depends on an available pkg-config client. The
  metadata contract is checked in Batch 176; any real Windows consumer claim
  requires Batch 177 execution.
- Deterministic ZIP creation is an internal Python release-tool step. It does
  not add Python runtime bindings or a simulator dependency.
- Simulation uses one deterministic scheduler/time domain. Distributed worker
  launch/recovery, automatic partitioning and conservative parallel simulation
  remain post-v2 work.
- Only official Accellera SystemC/TLM/SCV interfaces are supported. The removed
  fsim SystemC facade/custom-kernel API is intentionally not compatible.
- The source tree and installed command still carry the development version
  identity until Batch 177 performs the final version/tag transition.
