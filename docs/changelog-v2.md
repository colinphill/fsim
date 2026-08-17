<!-- SPDX-License-Identifier: Apache-2.0 -->
# v2.0 release-candidate changelog

This is the frozen Batch 176 candidate summary. Batch 177 owns the final
Release qualification, hosted Linux/Windows matrix, sanitizers, version/tag and
signed-or-explicitly-unsigned final disposition.

## Language and execution

- Current selectable VHDL, Verilog and SystemVerilog profiles now share typed
  semantic HIR, deterministic elaboration/scheduling, interpreter and optional
  exact-LLVM execution, artifacts, caches, debugger and VCD/FST observation.
- Mixed VHDL/SystemVerilog/SystemC hierarchy, SDF 2.1/3.0/4.0 application and
  VHDL VITAL timing execute through the same current-only design boundary.
- UVM 1.2 and IEEE 1800.2-2020-3.1, SVA, functional coverage, DPI-C, VPI and
  VHPI use the cataloged resource and transaction contracts.

## SystemC and interoperability

- Official Accellera SystemC 3.0.2, TLM 2.0.6 and governed SCV 2.0.1 are the
  only SystemC surface. The former fsim facade/custom kernel remains removed.
- Incremental plug-in compilation, source dependency capture, cache locking,
  ABI/producer rejection, transaction recording and failure containment are
  covered by direct fixture witnesses.

## Packaging and operations

- Source and binary contents now have explicit manifests, exclusions,
  install/uninstall ownership, relocatable CMake/pkg-config discovery, upstream
  licenses/notices/SBOMs and deterministic ZIP metadata.
- Six installed examples cover project/non-project phases, portable artifacts,
  library relocation, three-language hierarchy, SDF control and VITAL timing.
- Linux GCC 13 and Clang 22 package definitions plus pinned Windows LLVM-MinGW
  20260616 UCRT definitions assign every unavailable or Release result to final
  Batch 177 without inference.

See [known issues](known-issues-v2.md), the
[release boundary](release-and-post-v2.md), and the
[platform guide](user-platform-guide.md) before qualification or distribution.
