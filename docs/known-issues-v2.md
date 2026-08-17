<!-- SPDX-License-Identifier: Apache-2.0 -->
# v2.0.0 known issues and exclusions

This list records current release limits rather than deferred v1 work.

- Release ZIPs and the annotated tag use the explicit `unsigned-release`
  disposition. No detached cryptographic signature is published or inferred.
- The final pinned Linux/Windows hosted matrix runs from the sole release
  commit. Until those post-push rows are green, no Windows archive or final tag
  claim is complete.
- Windows packages target pinned LLVM-MinGW 20260616 UCRT. Native MSVC and
  clang-cl retain checked source/command portability contracts but are retired
  as package targets and have no current binary-package claim.
- Exact LLVM 22.1.8 is optional. Current-only native cache or plug-in payloads
  reject incompatible compiler, target, ABI, schema or content identities and
  must be regenerated from portable inputs.
- Windows pkg-config execution depends on an available pkg-config client. The
  metadata contract is always checked; a real Windows consumer claim requires
  its own retained hosted execution.
- Deterministic ZIP creation is an internal Python release-tool step. It does
  not add Python runtime bindings or a simulator dependency.
- Simulation uses one deterministic scheduler/time domain. Distributed worker
  launch/recovery, automatic partitioning and conservative parallel simulation
  remain post-v2 work.
- Only official Accellera SystemC/TLM/SCV interfaces are supported. The removed
  fsim SystemC facade/custom-kernel API is intentionally not compatible.
