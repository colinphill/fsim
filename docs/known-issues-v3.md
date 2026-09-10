<!-- SPDX-License-Identifier: Apache-2.0 -->
# Known v3.0 limitations

These are deliberate v3.0 product boundaries rather than silently accepted
language behavior:

- v2 manifests, persisted artifacts, caches, checkpoints, serialized state,
  and native plug-ins are rejected directly. Rebuild them from source for v3.
- VHDL-AMS is outside the digital VHDL profiles. Vendor-only packages,
  pragmas, encrypted key providers, PLI names, and foreign extensions are not
  implied by a standard selection.
- Coverage reports do not calculate a synthetic grand score. LCOV and
  Cobertura project only statement, explicit-line, and branch metrics; use
  JSON for condition, expression, toggle, FSM, functional, PSL, exclusion,
  and instance fidelity.
- v3.0 execution remains serial. Separate parallel elaboration/simulation job
  controls and deterministic/throughput policies belong to v3.1.
- The compiled performance lowering profile belongs to v3.2. v3.0 retains the
  observable interpreter, Debug, and LLVM O0-O3 modes.
- LLDB launch/attach and coordinated HDL/native handoff commands belong to
  v3.3. Existing HDL debugging and native symbols remain available through
  their documented lower-level workflows.
- Native DEB, RPM, and Inno Setup installers belong to v3.4. v3.0 publishes
  the existing deterministic source/archive artifacts.

Resource limits are reported separately from language legality. An
unsupported feature or exhausted ceiling must produce a diagnostic rather
than being dropped, narrowed, or executed under a different standard profile.
