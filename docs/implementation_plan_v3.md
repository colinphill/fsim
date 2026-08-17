<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v3 implementation plan

This is the authoritative batch and status record for fsim v3. Development
starts on branch codex/v3 from clean v2 checkpoint
22d5e2ba43b5e7711db6fe0b27d7e9e0d5345095.

## Governing contract

- Allocate exactly twenty implementation batches, Batches 178 through 197,
  with exactly twenty numbered changes in every batch.
- Changes 1 through 19 accumulate in one recoverable worktree with focused
  warnings-as-errors Debug builds and tests. Change 20 alone owns clean full
  Debug and Release qualification, documentation, one implementation commit,
  and one push.
- A release-closing Change 20 also owns sanitizers, hosted Linux and Windows
  qualification, release artifacts, and its annotated tag after every required
  lane is green.
- Batch 180 and Batch 190 remain tenth-batch sanitizer and hosted-CI
  boundaries.
- Local builds use at least eight workers. Local and hosted qualification
  commands retain 120-minute timeouts.
- Avoid formatting-only header changes. Make semantic header edits only when
  required and keep formatting work confined to changed implementation sources
  where practical.
- Define v3 schemas and native ABIs directly. Readers reject v2 manifests,
  objects, designs, checkpoints, caches, and plugins deterministically; v3
  adds no compatibility readers, migrations, fallbacks, or dual-write paths.
- Retain all existing HDL language profiles.
- The two privately supplied language standards are read-only references.
  Never copy, commit, package, quote, or log their contents, and never record
  their locations or file hashes in repository artifacts. Inventories may
  contain standard identifiers, clause numbers, independently written feature
  summaries, and test ownership. All tests and documentation examples must be
  independently authored.

## Release map

| Release | Closing batch | Included work |
| --- | ---: | --- |
| v3.0.0 | 188 | HDL coverage, IEEE TF/ACC PLI, VHDL-2019, and SystemVerilog-2023 |
| v3.1.0 | 191 | Deterministic and throughput-oriented parallel elaboration and simulation |
| v3.2.0 | 193 | Explicit compiled-LLVM performance lowering |
| v3.3.0 | 195 | LLDB native-plugin debugging on Linux and Windows |
| v3.4.0 | 197 | LLVM Release DEB, RPM, and Inno Setup packages |

## Public contracts

- Manifest additions: [coverage], [elaboration].jobs, [run].jobs,
  [run].parallel_policy, [build].lowering_profile, and [[plugins]].
- CLI additions: --code-coverage, --coverage-metrics, --coverage-db, --pli,
  --elab-jobs, --sim-jobs, --parallel-policy, and --lowering-profile.
- Add fsim coverage merge and fsim coverage report.
- Add fsim native-debug with --attach PID and --break-on-plugin-load, using
  LLDB exclusively.
- Extend fsim debug with LLDB handoff controls for HDL/native boundaries.
- Add VHDL 19/2019 and SystemVerilog 23/2023 standard spellings.
- Use one versioned .fsimcov database with separate code, SystemVerilog
  functional, and PSL coverage namespaces.

## v3.0.0

### Batch 178 - coverage identity, statement, line, and branch foundation

1. Register a clause-neutral coverage obligation and ownership matrix.
2. Define language-independent coverage point, metric, run, and result types.
3. Canonicalize source paths and content identities independently of checkout location.
4. Generate stable point IDs from language, construct kind, span, and source identity.
5. Discover executable Verilog/SystemVerilog statement points.
6. Discover executable VHDL statement points.
7. Model decision branches and individually addressable branch arms.
8. Derive covered, partial, and uncovered line states from statement points.
9. Attach coverage inventories to elaborated design instances.
10. Add a validated SimIR coverage-hit operation.
11. Implement saturating interpreter counters with overflow reporting.
12. Lower equivalent counters through LLVM O0-O3.
13. Preserve point identity and hits in the Debug engine.
14. Exclude non-executable declarations and statically removed constructs.
15. Assign stable hierarchical instance identities.
16. Compute source-aggregate unions without losing instance results.
17. Add opt-in manifest and CLI enablement with no default overhead.
18. Add coverage identities to objects, designs, and native-cache keys.
19. Prove Verilog/SystemVerilog/VHDL engine and aggregation equivalence.
20. Run standard batch closure and freeze the foundation inventory.

### Batch 179 - condition, expression, toggle, and FSM metrics

1. Register exact condition, expression, toggle, and FSM obligations.
2. Decompose Verilog/SystemVerilog decisions into stable atomic conditions.
3. Decompose VHDL Boolean decisions using equivalent rules.
4. Preserve short-circuit evaluation when recording condition outcomes.
5. Record true, false, and auxiliary unknown four-state outcomes.
6. Bound expression-combination expansion and report omitted combinations explicitly.
7. Define 0-to-1 and 1-to-0 toggle bins.
8. Instrument Verilog/SystemVerilog ports, nets, signals, and retained variables.
9. Instrument equivalent VHDL ports, signals, and retained variables.
10. Exclude automatic locals and memories by default.
11. Add explicit memory and array toggle-selection rules.
12. Track X/Z transitions diagnostically without scoring them as binary toggles.
13. Infer enum- and case-based current-state objects.
14. Infer optional next-state objects and legal-state sets.
15. Implement standard SystemVerilog FSM description pragmas.
16. Add VHDL source hints and language-neutral manifest FSM hints.
17. Record state visits and legal transitions separately.
18. Diagnose ambiguous, incomplete, and conflicting FSM descriptions.
19. Prove metric semantics across generate instances, engines, and mixed designs.
20. Run standard batch closure and freeze the broad metric set; MC/DC remains excluded.

### Batch 180 - unified coverage database, standard API, and reports

1. Define the bounded, versioned .fsimcov container schema.
2. Store model fingerprint, source inventory, run metadata, metrics, and exclusions.
3. Move SystemVerilog functional coverage into its database namespace.
4. Move PSL coverage into its database namespace.
5. Implement deterministic serialization and atomic replacement.
6. Implement strict same-design merging as the default.
7. Implement explicit partial merging of unchanged point identities.
8. Implement SystemVerilog coverage constants and $coverage_control.
9. Implement $coverage_get, $coverage_get_max, $coverage_merge, and $coverage_save.
10. Implement the corresponding VPI coverage controls, properties, and traversal.
11. Support module, hierarchy, instance, and coverage-type selection.
12. Add source fsim coverage off/on controls with metric and reason.
13. Add external source, hierarchy, object, and metric exclusion rules.
14. Preserve every excluded point and reason in database and reports.
15. Implement source, instance, and combined report models without a synthetic grand score.
16. Implement deterministic text, HTML, and full-fidelity JSON reports.
17. Implement LCOV and Cobertura projections for supported metric families.
18. Implement coverage merge/report, per-metric thresholds, and CI exit status.
19. Test corruption, size ceilings, path safety, merge conflicts, and mixed-language regressions.
20. Run clean Debug/Release, sanitizer, and hosted monitoring closure for coverage.

### Batch 181 - IEEE legacy TF PLI

1. Register IEEE TF requirements and explicitly exclude vendor extensions.
2. Define the v3 native-plugin ABI and common loader metadata.
3. Provide standard-compatible veriuser.h declarations and constants.
4. Provide Linux shared-library and Windows import-library link surfaces.
5. Discover and validate standard TF registration tables.
6. Validate every registered task/function descriptor transactionally.
7. Implement checktf, sizetf, calltf, and their failure containment.
8. Implement misctf lifecycle and synchronization reasons.
9. Implement argument count, type, direction, and expression inspection.
10. Implement integer, real, string, vector, and expression value access.
11. Implement parameter and instance-specific access.
12. Implement simulation time, delay, and timescale access.
13. Implement scope, instance, work-area, and user-data lifetimes.
14. Implement TF output, warning, error, and finish/stop controls.
15. Implement read-only and read-write synchronization callbacks.
16. Register TF system tasks/functions in Verilog and SystemVerilog profiles.
17. Serialize TF calls through the scheduler coordinator.
18. Contain plugin exceptions, invalid pointers, unload, and re-entry.
19. Prove independently authored C/C++ plugins on Linux and Windows.
20. Run standard batch closure and freeze the TF surface.

### Batch 182 - IEEE ACC and complete legacy PLI closure

1. Register the complete IEEE ACC routine and object inventory.
2. Provide standard-compatible acc_user.h.
3. Implement initialization, shutdown, configuration, and error reporting.
4. Map ACC handles onto generation-qualified hierarchy/VPI handles.
5. Implement absolute and relative lookup by name.
6. Implement top, scope, module, instance, and child traversal.
7. Implement port, net, variable, parameter, primitive, path, and timing objects.
8. Implement scalar, vector, real, string, strength, and delay reads.
9. Implement deposit, force, release, and scheduled value updates.
10. Implement indexed and iterator-style acc_next_* traversal.
11. Implement path-delay and timing-check access.
12. Implement value-change-link callback registration.
13. Implement callback cancellation, ordering, and re-entry containment.
14. Preserve handle and callback validity across simulation safe points.
15. Share values, scopes, and work areas coherently between TF and ACC.
16. Prove ACC/VPI views refer to the same simulation objects.
17. Define deterministic PLI behavior under future parallel execution.
18. Reject unsupported vendor names with stable diagnostics.
19. Run the full TF/ACC engine, artifact, cache, and platform corpus.
20. Run standard batch closure and declare legacy IEEE PLI complete.

### Batch 183 - VHDL-2019 syntax, types, interfaces, and expressions

1. Build a private-reference-derived, independently worded 2008-to-2019 clause inventory.
2. Add VHDL-2019 enum, manifest, CLI, artifact, and cache identities.
3. Implement revised lexical, grammar, and conditional-analysis behavior.
4. Implement the 2019 protected-type changes.
5. Implement unspecified types and their inference constraints.
6. Enforce the 64-bit minimum predefined INTEGER range.
7. Parse and model interface view declarations.
8. Implement record views and nested view composition.
9. Implement view-based port declarations and associations.
10. Implement subtype and direction rules within complex interfaces.
11. Implement conditional expressions and their contextual typing.
12. Expose result-array constraints inside functions.
13. Implement revised dynamically allocated storage semantics.
14. Implement sequential block statements and nested declarative regions.
15. Add all new predefined attributes and legality rules.
16. Apply revised overload, visibility, and conformance rules.
17. Prevent every 2019 construct from leaking into older profiles.
18. Round-trip all new semantic forms through objects and designs.
19. Add positive, negative, recovery, and profile-differential tests.
20. Run standard batch closure and freeze the VHDL-2019 frontend.

### Batch 184 - VHDL-2019 runtime, environment APIs, VHPI, and closure

1. Elaborate interface views and nested directional connections.
2. Execute view-based signal and variable updates.
3. Execute revised allocation and automatic reclamation behavior.
4. Execute sequential blocks across wait, return, and exception boundaries.
5. Implement the standard simulator API additions.
6. Implement standard data and time APIs.
7. Implement standard directory APIs.
8. Implement standard environment APIs.
9. Implement current-file, line, and call-path APIs.
10. Implement the standardized PSL API.
11. Implement report/assert statement APIs.
12. Implement the reflection API and reflected type/value model.
13. Update predefined packages and governed package compilation.
14. Implement revised tool, conditional-analysis, and protection directives.
15. Update VHPI capabilities, information model, and property access.
16. Update VHPI callbacks, value access, tool execution, and headers.
17. Integrate VHDL-2019 constructs with code and PSL coverage.
18. Prove interpreter, LLVM, debug, artifact, cache, and mixed-language behavior.
19. Close every active VHDL-2019 clause row and publish independent documentation.
20. Run standard batch closure and declare full VHDL-2019 support complete.

### Batch 185 - SystemVerilog-2023 frontend, data model, classes, and processes

1. Build a private-reference-derived, independently worded 2017-to-2023 clause inventory.
2. Add SystemVerilog-2023 enum, manifest, CLI, artifact, and cache identities.
3. Implement keyword, tokenization, preprocessing, and lexical changes.
4. Implement revised design-unit and scheduling declarations.
5. Implement scalar, integral, literal, and type-system changes.
6. Implement packed and unpacked aggregate changes.
7. Implement string, event, handle, and dynamic-object changes.
8. Implement class declaration and inheritance changes.
9. Implement parameterized-class and specialization changes.
10. Implement constructor, method, virtual, and lifetime changes.
11. Implement process, fork/join, and process-control changes.
12. Implement assignment and assignment-pattern changes.
13. Implement streaming and aggregate assignment changes.
14. Implement operator and expression changes.
15. Implement procedural statement changes.
16. Implement task, function, and argument changes.
17. Implement clocking and interprocess synchronization changes.
18. Prevent 2023 semantics from leaking into older profiles.
19. Prove new forms through semantic, artifact, and cache round trips.
20. Run standard batch closure and freeze the SystemVerilog-2023 core frontend.

### Batch 186 - SystemVerilog-2023 verification, hierarchy, and timing

1. Implement immediate-assertion revisions.
2. Implement concurrent-assertion revisions.
3. Update the formal concurrent-assertion execution model.
4. Implement checker revisions.
5. Implement constrained-random and solver revisions.
6. Implement functional-coverage revisions.
7. Implement utility system-task/function revisions.
8. Implement file and input/output task revisions.
9. Implement compiler-directive revisions.
10. Implement module and hierarchy revisions.
11. Implement program-block revisions.
12. Implement interface, modport, and virtual-interface revisions.
13. Implement package, import, export, and lookup revisions.
14. Implement generate-construct revisions.
15. Implement gate, switch, and UDP revisions.
16. Implement specify-block and timing-check revisions.
17. Implement SDF backannotation revisions.
18. Implement configuration and protected-envelope revisions.
19. Prove verification and timing behavior across interpreter and LLVM engines.
20. Run standard batch closure and freeze the design/verification surface.

### Batch 187 - SystemVerilog-2023 foreign APIs and complete closure

1. Implement DPI declaration and runtime revisions.
2. Update the standard DPI C layer and svdpi.h.
3. Implement revised foreign-code inclusion and context behavior.
4. Reconcile PLI/VPI overview rules with the v3 plugin model.
5. Update the complete VPI object model.
6. Implement every revised or added VPI routine.
7. Implement the assertion API.
8. Validate the complete standardized coverage API against Batch 180.
9. Implement the data-read API.
10. Update normative VPI and compatibility headers.
11. Update standard package behavior.
12. Update the standardized random-distribution implementation.
13. Apply normative syntax, keyword, and deprecation annex requirements.
14. Prove legacy TF/ACC interaction with the 2023 profile.
15. Prove code and functional coverage for new constructs.
16. Prove DPI/VPI callbacks across all scheduler phases.
17. Prove artifact, cache, checkpoint, debug, and trace behavior.
18. Prove independently authored C/C++ foreign applications on both platforms.
19. Close every active SystemVerilog-2023 clause row.
20. Run standard batch closure and declare full SystemVerilog-2023 support complete.

### Batch 188 - v3.0.0 integration and release

1. Audit zero unresolved rows across coverage, PLI, VHDL, and SystemVerilog.
2. Freeze v3 manifest, ABI, object, design, checkpoint, and cache schemas.
3. Prove deterministic rejection of all versioned v2 inputs.
4. Requalify every retained older HDL standard profile.
5. Run the complete HDL code/functional/PSL coverage corpus.
6. Run the complete TF/ACC PLI corpus.
7. Run the complete VHDL-2019 corpus.
8. Run the complete SystemVerilog-2023 corpus.
9. Qualify mixed-language and foreign-interface composition.
10. Qualify interpreter, LLVM O0-O3, and Debug equivalence.
11. Qualify artifacts, caches, checkpoints, traces, and debugger observations.
12. Complete fuzz, malformed-input, resource, and security readiness.
13. Freeze Linux and Windows warning-audit ownership.
14. Freeze hosted platform and toolchain definitions.
15. Publish v3.0 examples, user guides, API references, and known limitations.
16. Prepare deterministic source and existing archive artifacts.
17. Complete licenses, SBOM, provenance, and private-reference exclusion audits.
18. Freeze the v3.0 release record and exact tag message.
19. Set every product/version/package identity to 3.0.0.
20. Run final local/hosted qualification, commit, push, tag, and publish v3.0.0.

## v3.1.0

### Batch 189 - parallel execution foundation and elaboration

1. Register parallel correctness, determinism, resource, and performance obligations.
2. Add separate elaboration/simulation job controls while preserving build -j.
3. Add deterministic and throughput execution-policy types.
4. Implement a bounded worker pool with cancellation and exception propagation.
5. Assign canonical task and diagnostic sequence identities.
6. Freeze immutable semantic inputs before parallel work begins.
7. Construct an elaboration dependency DAG.
8. Parallelize independent library and root preparation.
9. Parallelize independent top-level elaboration.
10. Parallelize safe instance-subtree elaboration.
11. Express generic, parameter, configuration, and binding dependencies.
12. Isolate per-task hierarchy and symbol-table deltas.
13. Validate deltas before publication.
14. Commit hierarchy deltas in canonical order.
15. Sort diagnostics independently of completion order.
16. Bound queues, memory, cancellation latency, and worker shutdown.
17. Serialize foreign elaboration hooks through the coordinator.
18. Prove equivalence at one, two, four, and eight workers.
19. Establish retained elaboration performance baselines.
20. Run standard batch closure and freeze parallel elaboration.

### Batch 190 - deterministic parallel simulation

1. Build process/signal conflict and dependency graphs.
2. Partition elaborated designs into scheduler regions.
3. Add region-local queues and worker ownership.
4. Preserve active, inactive, nonblocking, reactive, and postponed phases.
5. Add explicit phase barriers and epoch advancement.
6. Stage signal writes and resolutions per region.
7. Commit updates with canonical event identities.
8. Preserve timed-event and delta-cycle order.
9. Parallelize safe process execution.
10. Preserve wait, event, mailbox, semaphore, and process-control semantics.
11. Partition random streams independently of worker count.
12. Preserve report, file-I/O, and diagnostic order.
13. Use deterministic per-worker coverage shards.
14. Preserve trace and callback sequence identities.
15. Coordinate DPI, PLI, VPI, VHPI, and SystemC calls.
16. Quiesce all workers at debugger and control safe points.
17. Serialize worker-independent logical scheduler state.
18. Prove checkpoint/restart equivalence across worker counts.
19. Run race, deadlock, cancellation, and ThreadSanitizer stress.
20. Run clean Debug/Release, sanitizer, and hosted monitoring closure.

### Batch 191 - throughput policy, performance gates, and v3.1 release

1. Implement an explicit throughput-oriented work-stealing scheduler.
2. Permit variation only where the language standards leave order unconstrained.
3. Preserve all constrained event-region and update semantics.
4. Add automatic worker selection with jobs=1 as the serial reference.
5. Add independent elaboration and simulation overrides.
6. Qualify multiple roots and mixed-language partitions.
7. Qualify SDF, VITAL, timing checks, and assertions.
8. Qualify UVM, functional coverage, and code coverage.
9. Qualify every native extension family.
10. Qualify traces, files, callbacks, checkpoints, and debugger safe points.
11. Contain worker failures and publish no partial epoch.
12. Add 1/2/4/8-worker benchmark orchestration.
13. Record variance-qualified benchmark-specific scaling floors.
14. Add oversubscription and small-design overhead gates.
15. Add CPU, memory, queue, and shutdown resource ceilings.
16. Publish deterministic-versus-throughput guidance and examples.
17. Close all parallel execution inventory rows.
18. Freeze v3.1 ABI/schema and release records.
19. Set every release identity to 3.1.0.
20. Run final local/hosted qualification, commit, push, tag, and publish v3.1.0.

## v3.2.0

### Batch 192 - performance lowering profile and frame elimination

1. Register observable/performance equivalence and incompatibility obligations.
2. Add observable and explicit performance lowering profiles.
3. Keep observable lowering as the default.
4. Restrict performance lowering to the compiled LLVM engine.
5. Include lowering profile and capabilities in artifact/cache identities.
6. Reject conflicting capabilities before lowering begins.
7. Perform process-local liveness and escape analysis.
8. Separate persistent state from transient values.
9. Promote eligible scalar locals to SSA values.
10. Promote eligible strings and containers.
11. Optimize class, access, and aggregate temporaries.
12. Retain only values live across suspension points.
13. Compact callable and suspended-task frames.
14. Remove process-local debugger materialization.
15. Remove runtime checkpoint serialization hooks.
16. Preserve code and functional coverage instrumentation.
17. Preserve global signal observation and tracing.
18. Diagnose debugger, local-trace, and save/restore incompatibilities.
19. Prove semantic equivalence across representative language constructs.
20. Run standard batch closure and freeze the lowering-profile contract.

### Batch 193 - fast runtime paths, benchmarks, and v3.2 release

1. Add direct compiled access to eligible signal and process state.
2. Specialize scheduler dispatch for lowered process capabilities.
3. Fuse safe SimIR operation sequences.
4. Fold and specialize runtime-invariant metadata.
5. Optimize callable entry, return, and copy semantics.
6. Optimize container and aggregate hot paths.
7. Optimize class and virtual-dispatch hot paths.
8. Retain governed foreign-call boundaries.
9. Retain coverage point identities through LLVM optimization.
10. Retain global trace identities through optimized lowering.
11. Integrate performance lowering with deterministic parallel execution.
12. Integrate it with throughput-oriented execution.
13. Allow direct LLDB-native debugging without HDL-local observation.
14. Reject coordinated HDL/native handoff for performance-lowered designs.
15. Establish benchmark-specific speed and memory gates.
16. Guard observable-mode and small-design non-regression.
17. Publish capability, failure, and selection documentation.
18. Close all performance-lowering inventory rows and release records.
19. Set every release identity to 3.2.0.
20. Run final local/hosted qualification, commit, push, tag, and publish v3.2.0.

## v3.3.0

### Batch 194 - LLDB launch/attach and native symbol infrastructure

1. Register native-debugger security, lifecycle, and platform obligations.
2. Standardize native plugin load/unload notifications.
3. Record plugin path, build ID, ABI, source map, and symbol metadata.
4. Add fsim native-debug launch mode.
5. Add PID-based attach mode.
6. Add stop-on-plugin-load selection.
7. Discover and validate exact LLDB 22.1.8.
8. Generate deterministic LLDB command scripts without shell interpolation.
9. Normalize source-path substitution and symbol search paths.
10. Propagate fsim arguments, environment, working directory, and exit status.
11. Implement Linux LLDB launch.
12. Implement Linux LLDB attach and detach.
13. Provision Windows LLDB from the matching LLVM 22.1.8 CLANG64 toolchain set.
14. Implement native Windows LLDB launch.
15. Implement native Windows LLDB attach and detach.
16. Validate PE/COFF GNU-ABI symbols, stack unwinding, threads, and breakpoints.
17. Handle Unicode and whitespace-containing Windows paths.
18. Contain missing debugger, permission, timeout, and early-process-exit failures.
19. Add scripted Linux and Windows launch/attach tests.
20. Run standard batch closure and freeze the LLDB platform bridge.

### Batch 195 - coordinated HDL/native handoff and v3.3 release

1. Add scheduler-wide native-debug quiescence.
2. Publish safe foreign-entry and foreign-return events.
3. Add native handoff commands to the HDL debugger.
4. Add plugin, symbol, and boundary breakpoint selection.
5. Integrate DPI-C handoff.
6. Integrate TF/ACC PLI handoff.
7. Integrate VPI handoff.
8. Integrate VHPI handoff.
9. Integrate SystemC handoff.
10. Integrate the stable v3 fsim plugin ABI.
11. Resume all workers only after LLDB releases the boundary.
12. Preserve deterministic and throughput policy guarantees.
13. Support direct native debugging of performance-lowered designs.
14. Require observable lowering for coordinated HDL/native handoff.
15. Contain native exceptions, crashes, detach, unload, and reload.
16. Correlate HDL source points, native frames, time, delta, and plugin identity.
17. Prove every extension family on Linux and Windows LLDB.
18. Publish SDK helpers, LLDB examples, and troubleshooting guidance.
19. Set every release identity to 3.3.0 and close debugger inventory rows.
20. Run final local/hosted qualification, commit, push, tag, and publish v3.3.0.

## v3.4.0

### Batch 196 - DEB, RPM, and Inno Setup construction

1. Register package contents, platforms, dependencies, and installation ownership.
2. Build one canonical self-contained LLVM-enabled Release staging tree.
3. Bundle required LLVM and LLDB 22.1.8 runtime components.
4. Define Linux and Windows installation layouts.
5. Generate the Ubuntu 24.04 x86-64 runtime DEB.
6. Generate the matching -dev SDK DEB.
7. Generate the matching debug-symbol DEB.
8. Implement DEB install, upgrade, conflict, and removal scripts.
9. Generate the Rocky/RHEL 9 x86-64 runtime RPM.
10. Generate the matching -devel SDK RPM.
11. Generate the matching debuginfo RPM.
12. Implement RPM install, upgrade, conflict, and removal scripts.
13. Create the LLVM-MinGW x86-64 Inno Setup definition.
14. Implement non-admin per-user installation as the default.
15. Implement explicit elevated all-users Program Files installation.
16. Add runtime and plugin-SDK installer components.
17. Add a separately installable Windows symbol package.
18. Implement optional PATH, discovery, upgrade, and clean uninstall behavior.
19. Build installed SDK consumers and native plugins on every platform.
20. Run standard batch closure and freeze package construction.

### Batch 197 - supply chain, signing, installer validation, and v3.4 release

1. Make unsigned package payload staging deterministic.
2. Freeze package names, versions, architectures, and dependency metadata.
3. Install complete LLVM/LLDB licenses and notices.
4. Generate package-level SPDX SBOMs.
5. Generate checksums and provenance attestations.
6. Add DEB signing and verification hooks.
7. Add RPM GPG signing and verification hooks.
8. Add Windows Authenticode signing and verification hooks.
9. Mark unsigned CI artifacts explicitly when credentials are absent.
10. Test clean Ubuntu 24.04 install, discovery, upgrade, and uninstall.
11. Test clean Rocky Linux 9 install, discovery, upgrade, and uninstall.
12. Test Windows per-user and all-users install, upgrade, and uninstall.
13. Build and execute offline C/C++ plugin SDK examples.
14. Debug packaged plugins with packaged LLDB on Linux and Windows.
15. Prove self-contained execution without development dependencies.
16. Reject downgrade, architecture mismatch, corruption, and partial installation.
17. Enforce package entry, size, permissions, and resource ceilings.
18. Publish installation, signing, support-matrix, and release documentation.
19. Set every product and package identity to 3.4.0.
20. Run final local/hosted/package qualification, commit, push, tag, and publish v3.4.0.
