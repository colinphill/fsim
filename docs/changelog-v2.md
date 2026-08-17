<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v2.0.0 release notes

fsim 2.0.0 is the current-only mixed-language release. Local Clang/GCC Debug,
Release, LLVM-disabled, exact-LLVM 22.1.8, ASan/UBSan, deterministic package,
install and supply-chain qualification is complete. The final commit is pushed
before the pinned hosted Linux/LLVM-MinGW matrix runs; the tag is created only
after those platform-specific rows are green.

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
- Linux GCC 13 and Clang 22 release archives are deterministic and relocatable.
  Pinned Windows LLVM-MinGW 20260616 UCRT archives are produced only by their
  own green post-push Release lanes; Linux evidence is never substituted.
- Release archives and the annotated tag have an explicit `unsigned-release`
  disposition. No detached cryptographic signature is claimed.

## Annotated tag message

```text
fsim v2.0.0

Current-only VHDL, Verilog, SystemVerilog, UVM, SDF, VITAL, FST,
SystemC 3.0.2, TLM and SCV 2.0.1 release with deterministic artifacts,
cataloged diagnostics and bounded single-scheduler execution.

Signature disposition: unsigned-release.
```

See [known issues](known-issues-v2.md), the [release evidence](release-and-post-v2.md),
and the [platform guide](user-platform-guide.md) before distribution.
