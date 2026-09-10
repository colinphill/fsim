<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v3.0.0 release notes

fsim v3.0.0 establishes a direct-v3 compatibility boundary and adds four
major product areas:

- language-neutral code coverage with statement, branch, line, condition,
  expression, toggle, and FSM metrics plus one unified `.fsimcov` container
  for code, SystemVerilog functional, and PSL coverage;
- standard IEEE TF and ACC legacy PLI surfaces on Linux and Windows;
- the VHDL-2019 language, runtime, predefined API, reflection, VHPI, artifact,
  cache, debugger, coverage, and mixed-language profile; and
- the SystemVerilog-2023 frontend, verification/timing, DPI, VPI, assertion,
  coverage, data-read, artifact, cache, debugger, and mixed-language profile.

Coverage databases can be merged and rendered as deterministic text, HTML,
full-fidelity JSON, LCOV, or Cobertura, with per-family CI thresholds. Older
VHDL, Verilog, and SystemVerilog profiles remain selectable and are qualified
without inheriting newer syntax or semantics.

This release deliberately rejects v2 manifests, objects, designs, libraries,
checkpoints, runtime state, caches, and native plug-ins. Rebuild persisted
inputs and native extensions for v3. See [Known v3.0 limitations](known-issues-v3.md)
and the [v3 API/ABI reference](v3-api.md).
