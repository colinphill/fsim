<!-- SPDX-License-Identifier: Apache-2.0 -->

# Simplification gate migration inventory

## Scope and baseline

This document began as the Batch 188B Change 2 inventory. Its crosswalks
describe the original migration proposal; implementation status is recorded
in `docs/implementation_plan_v3.md` and `docs/v3-resume.md`. Change 7 has
since registered additive required-ID/test validators and their current-owner
ledger. Change 8 has since retired fourteen historical v1 registrations and
renamed the two substantive corpus registrations to current names, preserving
their marker and evidence checks with additive required sets.
Change 9 has since unregistered the four historical v2 release/qualification/
performance/candidate-log gates; their source records and archived scripts
remain, as does `CheckV2SupplyChain.cmake`. Change 10 is complete: the
FST/SDF prose wrappers are retired, while current additive inventories own
their case IDs and behavioral tests. The v3 integration, retained-profile,
documentation, release-record, deterministic-archive, supply-chain,
version-identity, and hosted-lane gates have been migrated. The schema umbrella
now composes a compiled identity witness and registered compatibility owners;
the focused child CTests remain active. The v2-input rejection umbrella now
maps all fourteen required identities to executable negative-test owners; the
native-cache key/lookup equivalence is documented in the current plan and
resume checkpoint. Change 11 has registered a compiled Release-assertion
witness, an additive build-resource gate, and six focused domain gates backed
by 79 stable resource IDs and a negative validator fixture. After the
behavioral, boundary, and full closure checks passed, the large
resource-portability umbrella was unregistered; its historical source remains
packaged. Change 12 replaced the misleading source-line-budget CTest with a
direct translation-unit-structure gate and a positive/negative `.tpp` fixture;
the old delegating script remains packaged for historical ledger paths.
Change 13 consolidated 109 simple target-backed CTest registrations behind
one helper. The generated names, commands, and properties remained
byte-identical; specialized argument, fixture, and environment registration
was left explicit.

Batch 188I final-source correctness qualification passed. Its fixed-baseline
performance matrix was stopped during sample 3 and did not produce a passing
result. The user deferred performance acceptance to future work; the cumulative
performance obligation remains open for a future qualified run.

The inventory was prepared from `codex/v3` at
`ed5ca693704edd277ec3f055ed7d9ed0e3048f2d`. `origin/codex/v3` matched that
revision. The only unrelated worktree entry was the intentional untracked
`phase.fst`; it is outside this migration and must remain untouched.

The migration follows four rules:

1. Preserve historical documents, ledgers, source manifests, and case IDs as
   records. Retiring a validator does not authorize deleting its evidence.
2. Preserve behavioral, license, ABI/schema, resource, portability, and
   AST-lifetime obligations. A gate may retire only after each substantive
   obligation has a focused existing or replacement owner.
3. Replace literal totals and whole-file digest pins with required-set
   validation: required stable IDs/test names must exist exactly once, while
   additive rows and tests remain legal.
4. Do not restore a physical source-line quota. Readable WebKit-style
   partitioning remains required, and independently compiled translation
   units remain the structural mechanism.

## Current generated CTest topology

Read-only selector-level inspection measured the generated Release CTest
topology as follows:

- `ctest --test-dir build/ci-linux-release --show-only=json-v1`
  reports 423 registered tests, of which exactly nine carry the exact
  `recursive-closure` label.
- The same command with `-LE '^recursive-closure$'` emits 414 test names.
- The same command with `-L '^recursive-closure$'` emits 171 test names, not
  nine.
- The two emitted name sets overlap by 162 tests. There are 252 names unique
  to the `-LE` set and nine unique to the `-L` set.

The nine `-L`-unique names are
`fsim.systemc.accellera_closure`, `fsim.scv.closure`, `fsim.sdf-closure`,
`fsim.sdf-application-closure`, `fsim.sdf-vital-closure`,
`fsim.vhdl-standard-mode-closure-matrix`,
`fsim.verilog-systemverilog-standard-mode-closure-matrix`,
`fsim.systemverilog-closure-matrix`, and `fsim.verilog-closure-matrix`.
Each has one `FIXTURES_REQUIRED` closure-witness fixture. CTest expands those
fixture requirements under the `-L` selection and adds the 162 registered
`FIXTURES_SETUP` witness tests. Those witnesses are also ordinary non-recursive
tests selected by `-LE`, which causes the measured outer-invocation overlap.

This outer selector overlap is distinct from nested execution. The standard
recursive drivers, including `RunAccelleraSystemCClosure.cmake`,
`RunScvClosure.cmake`, the three SDF drivers, and the four
language/standard-mode drivers, already receive
`FSIM_DEDUPLICATED_CTEST=ON` and return before starting nested CTest. That
early return prevents a recursive driver from launching another copy of its
witness suite; it does not prevent CTest from adding fixture setup tests to
the outer `-L` invocation. Change 16 must eliminate the 162-test outer
overlap so each required test executes once, without removing fixture
ownership or repeating the false claim that the drivers themselves still
launch nested suites.

`cmake/CheckCTestCommandUniqueness.cmake` additionally protects a different
property: one owner per generated command plus fixture ownership for
`fsim.runtime` and `fsim.runtime.fst_reader`. That substantive protection is
retained below.

Every exact existing `fsim.*` CTest name used below was compared with the
names emitted by the unfiltered generated JSON at this baseline. Names under
`fsim.contract.*` are explicitly proposed and are not registered. Text ending
in `*`, such as `fsim.v1-*` or the coverage examples below, is a name pattern,
not a literal registered CTest. Descriptive phrases such as “runtime ABI
tests” identify a migration category whose exact required set must be frozen
by its owning later change; they are not asserted to be current CTest names.

## Obligation classes

The following terms are used in the crosswalk:

- **Behavioral**: executable positive, negative, differential, determinism,
  corruption, relocation, or compatibility evidence.
- **Contract**: ABI/schema/version, public API/CLI, package/archive, license,
  provenance, platform/toolchain, or bounded-resource behavior.
- **Identity**: stable case/test IDs needed for selection, review, and failure
  attribution. Identity is preserved even when a historical total is not.
- **Historical pin**: prose wording, a retired batch/change owner, an exact
  count, an output sentence, or a whole-file digest that does not itself test
  product behavior.

Historical pins are candidates for retirement. Behavioral, contract, and
identity obligations are not.

## v1 gate crosswalk

All `fsim.v1-*` CTests below are proposed to retire after migration. Their
source scripts and historical input files may remain archived until the
source-package manifest is deliberately updated; this inventory does not
delete them.

| Current CTest / checker | What it really protects | Classification | Existing or proposed owner before retirement |
| --- | --- | --- | --- |
| `fsim.v1-vhdl-matrix` / `CheckV1VhdlMatrix.cmake` | Eight `V1-VH-*` rows in `docs/feature-matrix.md`, their order, `execute` spelling, and nonempty prose cells | Historical prose/total pin; case identity only | Preserve `V1-VH-01` through `V1-VH-08`; behavior remains in the registered VHDL tests and closure matrices. Proposed required-set validator checks the IDs without an exact total or row order. |
| `fsim.v1-mixed-conversion-matrix` / `CheckV1MixedConversionMatrix.cmake` | Two `ML-005/006` prose rows | Historical prose/total pin; case identity only | Preserve both IDs and the mixed-language tests. Proposed required-set validation checks presence/uniqueness only. |
| `fsim.v1-legality-audit` / `CheckV1LegalityAudit.cmake` | Feature-matrix status/evidence cells and historical totals for SV, VHDL, mixed, and SystemC rows | Mostly historical total/prose pins; underlying legality behavior is substantive | Existing frontend/elaboration/application tests remain authoritative. Proposed required-case/test-set ledgers retain stable IDs and positive/negative owners without freezing totals. |
| `fsim.v1-conformance-corpus` / `CheckV1ConformanceCorpus.cmake` | Marker-to-fixture/CTest mapping, uniqueness, diagnostic ownership, execution modes, plus exact marker count/digest | Mapping and case identity are substantive; count/digest are historical pins | Preserve `tests/feature_matrix/v1_conformance_corpus.txt`, fixture identity, and registered tests. Proposed validator checks unique IDs, safe existing paths, registered test names, and required evidence modes; it does not pin total or digest. |
| `fsim.v1-conformance-audit` / `CheckV1ConformanceAudit.cmake` | SPDX coverage, approved third-party roots, provenance decisions, and historical source/queue wording and minimum counts | License/provenance is substantive; prose/counts are historical | `fsim.source-package-manifest`, `fsim.systemc-upstream-provenance`, `fsim.scv-upstream-provenance`, `CheckIeeePackageInventory.cmake`, repository `LICENSE`, SBOMs, and proposed focused license/provenance required-set validation. Preserve `docs/v1-conformance-audit.md`. |
| `fsim.v1-portability-corpus` / `CheckV1PortabilityCorpus.cmake` | `PORT-*` identities mapped to registered CTests/evidence markers, plus exactly 20 rows and a fixed mode list | Mapping and identity are substantive; exact 20 is historical | Preserve `PORT-001` through `PORT-020`. Proposed required-set validator verifies unique IDs, safe evidence paths, registered test names, and required portability surfaces while allowing additions. |
| `fsim.v1-portability-audit` / `CheckV1PortabilityAudit.cmake` | Historical CI jobs/presets, platform branches, sanitizer placement, worker count, and repair-queue prose | Current platform/toolchain policy is substantive; old lane/count wording is historical | `fsim.v3-hosted-toolchains`, `fsim.v3-warning-audit`, `fsim.windows-llvm-contract`, `fsim.tool-portability-contract`, SystemC/SCV portability/provenance tests, and the proposed hosted-lane structural validator. Current CI truth is four LLVM-enabled Linux/Windows Debug/Release lanes, not the v1 lane model. |
| `fsim.v1-release-audit` / `CheckV1ReleaseAudit.cmake` | Composition of legality/portability gates, feature-matrix digest, exact per-surface totals, and closure prose | Historical aggregate pin | No replacement aggregate wrapper. Required IDs move to focused required-set validators; behavior stays in normal CTests and recursive closures. Preserve `docs/v1-release-audit.md`. |
| `fsim.v1-systemverilog-release` / `CheckV1SystemVerilogRelease.cmake` | SV/V1-SV prose rows, nonempty owners, historical counts, registration | Historical aggregate around substantive tests | Registered frontend/application/runtime tests plus `fsim.systemverilog-closure-matrix` and `fsim.verilog-systemverilog-standard-mode-closure-matrix`; proposed required-case set preserves IDs only. |
| `fsim.v1-vhdl-release` / `CheckV1VhdlRelease.cmake` | VH/V1-VH prose rows, nonempty owners, historical counts, registration | Historical aggregate around substantive tests | Registered VHDL tests plus `fsim.vhdl-standard-mode-closure-matrix`; proposed required-case set preserves IDs only. |
| `fsim.v1-mixed-systemc-release` / `CheckV1MixedSystemCRelease.cmake` | ML/SC prose rows, owner cells, historical totals, and composed v1 audit | Historical aggregate around substantive ABI/behavior | Mixed-language application tests, `fsim.systemc.accellera_closure`, `fsim.scv.closure`, `fsim.systemc-accellera-portability-contract`, `fsim.scv-portability-contract`, installed-consumer and plugin ABI tests. |
| `fsim.v1-differential-release` / `CheckV1DifferentialRelease.cmake` | Historical differential ledger totals and composition across interpreter, LLVM, cache, debugger, VCD, scheduling, and failures | Differential obligation is substantive; totals/prose are historical | Preserve case IDs. Existing application/runtime/cache/debug/trace tests own behavior. Proposed differential required-set lists required named tests/cases rather than expected totals or prose reviews. |
| `fsim.v1-public-release` / `CheckV1PublicRelease.cmake` | Installed commands/headers/libraries, API/ABI versions, CLI exit/status behavior, Unicode paths, Windows environment seams | Contract, ABI, and behavior are substantive | `fsim.installed-public-contract`, `fsim.api`, `fsim.api.c_header`, SystemC installed-consumer tests, `fsim.v3-version-identity`, and focused CLI tests. Add missing required names to the proposed public-contract required set before retirement. |
| `fsim.v1-inventory-release` / `CheckV1InventoryRelease.cmake` | Composed diagnostics/source/IEEE/license checks plus exact diagnostic/source/SPDX/IEEE/conformance totals and exact status text | License/source ownership is substantive; all totals and output strings are historical | `fsim.diagnostics-catalog`, `fsim.source-package-manifest`, `fsim.ieee-package-inventory`, upstream provenance tests, and proposed license required-set validation. Never replace this with another global source-count pin. |
| `fsim.v1-resource-release` / `CheckV1ResourceRelease.cmake` | Composition of current platform/resource checks but requires historical status sentences and lane counts | Underlying resource/platform contracts are substantive; wrapper is historical | Retain the focused component tests until their own obligations migrate. Proposed resource required-set and hosted-lane validators replace the wrapper; preserve Release assertions and bounded stack/timeout policies explicitly. |
| `fsim.v1-release-candidate` / `CheckV1ReleaseCandidate.cmake` | Exact feature-matrix/evidence path totals, corpus totals, and v1 audit composition | Historical release-record pin | Preserve its audit document and identities. No active v3 replacement is required beyond current tests and release gates. |

## v2 gate crosswalk

| Current CTest / checker | What it really protects | Classification | Existing or proposed owner before retirement |
| --- | --- | --- | --- |
| `fsim.v2-release-records` / `CheckV2ReleaseRecords.cmake` | Under v3 it returns after checking five tokens in `packaging/v2-release-record.txt`: schema, candidate, compiled version, tag, and unsigned disposition | Historical release-record pin | Preserve `packaging/v2-release-record.txt`, `packaging/v2-support-matrix.tsv`, `packaging/example-output-freeze.tsv`, and v2 release docs as immutable records. Remove the active CTest after source-manifest updates; do not migrate the historical version into v3 product checks. |
| `fsim.v2-qualification-inventory` / `CheckV2QualificationInventory.cmake` | Nineteen Batch 175 rows, exact active/preserved/deferred partition, old plan wording, digest, owners, and retained-log paths | Historical batch/process pin; IDs remain useful | Preserve `Q175-02` through `Q175-20` and the TSV. No active gate should enforce old dispositions. If cross-version traceability is required, proposed required-set validation checks unique IDs and safe existing source owners only. |
| `fsim.v2-performance-baselines` / `CheckV2PerformanceBaselines.cmake` | Forty-four Linux Debug observations from Batch 175, exact compiler/change set, thresholds, evidence paths, and digest | Historical performance record, not a current performance gate | Preserve `tests/feature_matrix/v2_performance_baselines.tsv` and IDs. Current/future performance qualification must measure the current executable with workload-specific variance rules; it must not reuse the old observations as pass/fail inputs. |
| `CheckV2SupplyChain.cmake` | Archive path safety/order/uniqueness, source/binary package contents, licenses/notices/SBOMs/patches, and output record generation | Supply-chain behavior is substantive, but this is a historical v2 packaging driver and is not a standalone CTest | Preserve the script while any source manifest or historical packaging workflow names it. Current ownership is `fsim.source-package-manifest`, `fsim.v3-supply-chain`, deterministic packaging, `CheckV3InstalledArchive.cmake`, and upstream provenance tests. Retire/delete only after those required files and archive-safety checks are proven and the manifest is deliberately revised. |

`fsim.release-candidate-log-audit` is also a historical evidence-record gate:
it parses optional Batch 176 logs and otherwise returns successfully after
printing policy. Preserve the logs and documentation, but retire the active
CTest after any still-required warning/failure classifications are represented
by named current tests or the hosted-lane validator.

## Other release and portability audit crosswalk

These current tests are not named `v1` or `v2`, but they carry historical
release prose or are composed by the retiring release wrappers. They must be
handled explicitly rather than disappearing as incidental dependencies.

| Current CTest / checker | What it really protects | Proposed disposition |
| --- | --- | --- |
| `fsim.fst-release-audit` / `CheckFstReleaseAudit.cmake` | Composes FST inventory, diagnostics, translation-unit structure, FST portability, and application de-duplication, then pins 17 rows, old diagnostics/source counts, exact status sentences, and v2 documentation phrases | Retire this release wrapper after the FST inventory uses required IDs and `fsim.fst-portability-contract`, FST behavioral tests, diagnostics, translation-unit structure, license, and de-duplication owners are independently required. Preserve `docs/v2-fst-release-audit.md` and public tracing/API documentation. |
| `fsim.sdf-application-release-audit` / `CheckSdfApplicationReleaseAudit.cmake` | Composes SDF application inventory, diagnostics, and translation-unit structure, then pins 17 rows, old counts/status text, and documentation prose | Retire the wrapper after required `SDFAPP-*` identities and the named SDF application tests/closure are bound by a required set. Preserve `docs/v2-sdf-application-release-audit.md`, `docs/sdf.md`, and examples. |
| `fsim.sdf-vital-release-audit` / `CheckSdfVitalReleaseAudit.cmake` | Composes SDF VITAL inventory, diagnostics, translation-unit structure, and the resource umbrella, then pins 17 rows, old counts/status text, and documentation prose | Retire the wrapper after required SDF/VITAL IDs, `fsim.sdf-vital-closure`, behavioral application tests, and focused resource gates pass. Preserve `docs/v2-sdf-vital-release-audit.md` and examples. |
| `fsim.msvc-release-contract` / `CheckMsvcReleaseContract.cmake` | Release assertions, MSVC-style forced-header policy, CRT propagation, Windows Release configuration, and SystemC plugin compile/link flags | Do not retire with the historical wrappers. First move Release assertions into `fsim.contract.release-test-assertions`, runtime propagation into the installed-public contract, and plugin command plans into executable SystemC compiler tests. Retain the current gate until all three are green. |
| `fsim.tool-portability-contract` / `CheckToolPortabilityContract.cmake` | UTF-16/UTF-8 conversion, native path round trips, binary I/O, exit status, Unicode API, and direct-source behavior | Substantive. Retain until exact CLI/API/Tcl/runtime behavioral tests form a required set; migrate source-token assertions to those tests. |
| `fsim.systemc-portability-contract` / `CheckSystemCPortabilityContract.cmake` | Compiler discovery/fingerprint, cache/process/incremental compile-link behavior, strict C ABI, exceptions, threads, and loader lifetime | Substantive. Retain until the SystemC resource gate binds the exact plugin matrix/compiler/cache/ABI/lifetime tests. |
| `fsim.fst-portability-contract` / `CheckFstPortabilityContract.cmake` | Bounded decoding, binary I/O, transactional diagnostics, corruption/resource negatives, semantic differential behavior, and dependency independence | Substantive. Retain until the trace-resource gate binds exact reader/VCD/phase/artifact tests. |
| `fsim.scv-portability-contract` / `CheckScvPortabilityContract.cmake` | SCV cache/relocation/ownership and compatibility boundaries | Substantive. Retain until the SystemC resource required set owns the same exact cases. |
| `fsim.systemc-accellera-portability-contract` / `CheckSystemCAccelleraPortabilityContract.cmake` | Accellera ABI, compatibility rejection, session/rollback, ordered execution, safe points, codecs, TLM, observation, trace hooks, installation, and focused evidence | Substantive. Retain until these cases are explicit in the SystemC resource and foreign-ABI/lifetime required sets. |

The last five portability contracts are migration sources, not immediate
retirement targets. Their literal source-token checks may be removed only as
the corresponding executable required sets become complete.

## Current v3 and cross-cutting gate crosswalk

These gates must not be removed wholesale. Their brittle count/digest/literal
parts may be simplified only after the substantive column has a passing owner.

| Current CTest / checker | Substantive obligation that survives | Brittle mechanism to replace | Proposed disposition |
| --- | --- | --- | --- |
| `fsim.v3-release-integration-inventory` / `CheckV3ReleaseIntegrationInventory.cmake` | Coverage foundation/metrics, TF, ACC, VHDL-2019, and SystemVerilog-2023 inventories have unique stable IDs, valid owners, no unresolved required rows, and passing domain checkers | Six-row and 163-row totals, whole-ledger/child digests, `B188-C01`, and exact child status text | Replace with a required-domain/ID validator over the existing inventories and direct execution of their focused CTests. Additive rows are allowed. |
| `fsim.v3-schema-freeze` / `CheckV3SchemaFreeze.cmake` | The public ABI/schema and cache namespace identities exported by `include/fsim/project/project.hpp`, `include/fsim/runtime/*.h`, `include/fsim/artifact/object.hpp`, `include/fsim/library/artifact.hpp`, `include/fsim/artifact/design.hpp`, `include/fsim/app/design_artifact.hpp`, and `src/compiler/llvm_jit_cache_key.cpp`, together with the explicit identities in `tests/feature_matrix/v3_schema_freeze_inventory.tsv` | Six-domain/owner-row totals, child digests/status sentences, copied numeric literals, and batch-owner strings | Retain until a replacement ABI/schema contract reads the authoritative exported constants and explicit known-version ledger directly and runs artifact/checkpoint/cache compatibility tests. Required IDs replace totals; the inventory must not copy schema numbers that can drift from their owners. |
| `fsim.v3-v2-input-rejection` / `CheckV3V2InputRejection.cmake` | v2 manifest/object/portable/design/library/checkpoint/cache/plugin inputs are rejected before state publication with stable diagnostics/containment | Fourteen-row/six-family totals, ledger digest, source-token searches, owner strings | Replace with executable negative tests for every existing `V3REJECT-*` ID and a required-ID set. Do not reduce to version-number token checks. |
| `fsim.v3-retained-profile-qualification` / `CheckV3RetainedProfiles.cmake` | Language profile aliases select the correct semantics and survive artifact paths | Thirteen-row language totals, digest, source-token matching, owner strings | Keep all `V3PROFILE-*` identities; map each to named project/application/artifact test cases. A required-ID validator permits future profiles. |
| `fsim.v3-warning-audit` / `CheckV3WarningAudit.cmake` | Four hosted LLVM-enabled lanes configure warnings as errors | Exact YAML occurrence counts, four-row digest, retained-log names, owner strings | Merge into the proposed hosted-lane structural validator. It must require Linux/Windows x Debug/Release, LLVM ON, warnings-as-errors, and current compiler identities without pinning YAML spelling or artifact/log names. |
| `fsim.v3-hosted-toolchains` / `CheckV3HostedToolchains.cmake` | Ubuntu Clang 22 + LLVM 22.1.8 and Windows LLVM-MinGW 20260616 + LLVM 22.1.8 lanes, Debug/Release, timeout and bounded parallelism | Four-row digest, exact YAML text/occurrence counts, historical owner/log names | Merge with warning audit. The workflow currently uses `--parallel 2`; validate the configured current bound semantically. |
| `fsim.v3-release-documentation` / `CheckV3ReleaseDocumentation.cmake` | Required guides/examples exist, are licensed, and document supported public surfaces | Six-entry total and free-form literal prose tokens | Replace only brittle wording with required document IDs/paths and durable structured headings or links. Preserve documents. |
| `fsim.v3-deterministic-artifacts` / `CheckV3DeterministicArtifacts.cmake` | Deterministic source/binary archive construction and safe layout | Archive-layout digest and helper source-token searches | Keep behavioral archive byte comparison/path-safety tests; use required archive IDs/entries, not total-entry pins except where a published format explicitly requires an exact set. |
| `fsim.v3-supply-chain` / `CheckV3SupplyChain.cmake` | License, NOTICE, SBOM, provenance, source-manifest coverage, and exclusion of private-reference material | Thirteen-row and inventory digest pins | Retain as a focused license/provenance gate after converting to required IDs and semantic SBOM/source-manifest checks. Never remove source manifests as simplification. |
| `fsim.v3-release-record` / `CheckV3ReleaseRecord.cmake` | Published v3 version/tag/package/archive identities and referenced release documents | Whole-document and governance digests | Keep required scalar keys, unique keys, safe existing paths, and consistency with compiled/package version. Remove whole-document digest pins so additive release notes do not fail. |
| `fsim.v3-version-identity` / `CheckV3VersionIdentity.cmake` | Product, compiled header, pkg-config, package, archive, CI, and target identities agree; `fsim --version` reports 3.0.0 | Eleven-row total and ledger digest | Retain semantic equality and negative stale-v2 checks. Replace total with required `V3VER-*` IDs. |
| `CheckV3InstalledArchive.cmake` (hosted Windows Release) | Safe archive extraction, installed binaries/libraries/docs, offline consumers, regression evidence, relocation, and uninstall | `expected_tests: '423'` and `expected_archive_entries: '1269'` literal totals in CI | Preserve the behavioral install audit. Replace totals with required installed-entry and required-test sets plus unexpected duplicate/unsafe-path rejection. Published archive manifests may still enumerate exact paths; CTest growth must not require changing a count. |
| `fsim.source-line-budget` / `CheckSourceLineBudget.cmake` | It currently delegates only to translation-unit structure | Misleading historical name and exact status string consumed by old audits | Rename/register a translation-unit-structure gate after dependent old wrappers retire. Do not add a physical line ceiling. |
| `CheckTranslationUnitStructure.cmake` | No `.tpp` implementation files under `include`, `src`, or `tests`; template definitions stay in owning headers and non-template work moves to `.cpp` | None beyond the global glob | Retain the obligation. A renamed CTest may call this checker directly. Preserve source manifests when registration/script names change. |
| `fsim.ctest-command-uniqueness` / `CheckCTestCommandUniqueness.cmake` | Each resolved generated command has one owner; child-directory witnesses have required fixture ownership | None inherently count-based; unresolved executables are merely reported | Retain. Extend only if the new required-set helper needs duplicate-name or selector-partition checks. Do not confuse command uniqueness with recursive-driver de-duplication. |
| `fsim.ast-lifetime-governance` / `CheckAstLifetimeGovernance.cmake` | No parser AST ownership crosses compiled-HIR elaboration/cache boundaries; parser storage is destroyed before elaboration; five direct/object/cache/library/design paths remain differential | Contract digest, exact 37 scopes, source-token heuristics | Retain until equivalent compile-time/API constraints and executable AST-destruction/differential tests cover every `ASTLIFE-*` identity. Convert the scope ledger to required IDs and allow additive scopes. The replacement is currently unimplemented. |
| `fsim.resource-portability-contract` / `CheckResourcePortabilityContract.cmake` | Real resource, ABI, portability, coverage, FST, ACC/TF/VPI/VHPI, SystemC, language-profile, artifact, transaction, lifetime, and bounded-failure obligations | One 6,682-line token search, historical worker/status prose, and unrelated domain aggregation | Split by domain as described below. Retire the umbrella only after every domain gate is registered and its required tests pass. |

## Resource-portability decomposition

The following focused gates now own the umbrella's active obligations. All
eight named gates are registered, and the old umbrella is unregistered. The
source checker remains packaged as historical evidence.

| Proposed focused gate | Required obligations | Existing evidence to bind by exact test/case name |
| --- | --- | --- |
| `fsim.contract.build-resources` | Hosted timeout, bounded build/test parallelism, link pool, Debug object policy, 128 MiB Windows test stacks, and bounded large-test settings | `.github/workflows/ci.yml`, root `CMakeLists.txt`, `fsim.msvc-debug-contract`, `fsim.msvc-release-contract`, `fsim.windows-llvm-contract`, and resource-labeled tests |
| `fsim.contract.release-test-assertions` | Assertions remain active in Release tests on GNU-style frontends through `-UNDEBUG`, and on MSVC-style frontends through forced `fsim_test_assertions.h` containing `#undef NDEBUG`; `/DNDEBUG /UNDEBUG` warning conflicts stay absent | `CMakeLists.txt`, `CheckMsvcReleaseContract.cmake`, and one proposed executable Release assertion witness. This obligation must survive even if the MSVC release prose gate is later simplified. |
| `fsim.contract.coverage-resources` | Bounded coverage schemas, identities, saturation, allocation, merge/report determinism, source/instance attachment, mixed-engine and mixed-language equivalence | `fsim.code-coverage-inventory` and `fsim.code-coverage-metrics-inventory`, plus the registered-test name patterns `fsim.artifact.coverage*`, `fsim.frontend.coverage*`, and `fsim.application.*coverage*` (patterns, not literal CTest names) |
| `fsim.contract.foreign-abi-lifetime` | Public C/C++ ABI versions; bounded ACC/TF/VPI/VHPI cursors/handles; generation rejection; callback/context lifetime; atomic failure containment | `fsim.abi-schema-inventory`, runtime ABI tests, `fsim.api.c_header`, `fsim.legacy-tf-inventory`, `fsim.legacy-acc-inventory`, SystemVerilog VPI and VHDL VHPI tests, plus `fsim.v3-schema-freeze` until migrated |
| `fsim.contract.trace-resources` | FST arbitrary-width values, change/hierarchy storage bounds, trace/transaction ordering, scoped/SystemC phase traces | `fsim.fst-inventory`, `fsim.fst-portability-contract`, `fsim.runtime.fst_reader`, FST/SCV recording/resource tests, and trace-labeled application tests |
| `fsim.contract.language-resources` | VHDL-2019 and SystemVerilog-2023 feature ownership, profile/artifact/cache identity, bounded hierarchy/lookup and constraint behavior | `fsim.vhdl-2019-inventory`, `fsim.systemverilog-2023-inventory`, retained-profile tests, standard-mode inventories and closures, and their named application/runtime tests |
| `fsim.contract.systemc-resources` | Pinned upstream ownership, ABI/install/cache relocation, bounded phase/TLM behavior, SCV compatibility and patch governance | `fsim.systemc-accellera-portability-contract`, `fsim.systemc-upstream-provenance`, `fsim.scv-portability-contract`, `fsim.scv-upstream-provenance`, `fsim.scv-patch-governance`, installed consumers, and the two recursive closure drivers |
| `fsim.contract.artifact-resources` | Artifact/cache schema bounds, safe path/size handling, deterministic serialization, relocation, corruption containment, and no partial publication | object/library/design/cache tests, `fsim.v3-schema-freeze`, `fsim.v3-v2-input-rejection`, deterministic packaging, installed archive audit, and Batch 188A Change 19 tests |

Each focused gate should consume a small checked-in ledger with stable IDs and
named CTests/cases. The validator must reject duplicate IDs, missing required
IDs, unregistered tests, unsafe paths, and missing substantive owners. It must
not reject additive IDs merely because a row count or file digest changed.

## Proposed common validators

The following helpers are proposed and unimplemented:

1. `cmake/CheckRequiredCTestSet.cmake`: consumes a newline/TSV list of exact
   CTest names; rejects duplicate requirements, duplicate registered names,
   and missing registrations. It does not pin the total generated test count.
2. `cmake/CheckRequiredIdSet.cmake`: consumes a ledger, ID column, and required
   ID list; rejects missing/duplicate required IDs, malformed IDs, unsafe
   paths, and missing named owners. It allows additive valid rows.
3. `cmake/CheckHostedLaneContract.cmake`: structurally validates the current
   four hosted lanes, LLVM/warnings configuration, timeout/parallel bounds,
   and the actual fixture-expanded ordinary/recursive selector sets. Its
   replacement execution model must eliminate the measured 162-test overlap
   so each required test executes once. It must not pin YAML line counts,
   artifact names, retained-log names, or historical batch owner strings.
4. Focused domain ledgers for the resource decomposition above. These retain
   existing public case IDs wherever available and introduce new IDs only for
   obligations that currently exist solely as source-token searches.

## Actionable Changes 7-19 sequence

This sequence begins only after Batch 188B Change 6 establishes the corrected
baseline: every required case/configuration passes its oracle with a timed
sample, a separate native-coverage profile where applicable, and verified
source and binary identities. This baseline-readiness prerequisite is not
the seven-alternating-sample baseline/candidate performance gate, which must
be run for simplification candidates at Change 20. Each change is a focused
migration boundary; retirement happens only in the change that proves its
replacements.

7. **Migrate substantive licensing, packaging, ABI, and language obligations
   into current checks.** Introduce the required-ID/test helpers as internal
   support here, with positive/negative fixtures for missing, duplicate,
   additive, unsafe-path, and missing-owner cases. Bind the substantive
   obligations identified above before any historical gate retires.
8. **Remove superseded v1 release-record gates.** Retire the inventoried v1
   CTests/checkers only after Change 7 owners pass. Preserve historical docs,
   manifests, source-package membership, and all v1 case identities.
9. **Remove superseded v2 release-record gates.** Retire the v2 release-record,
   qualification, performance-baseline, and candidate-log CTests while
   preserving their records, manifests, IDs, and evidence. Keep
   `CheckV2SupplyChain.cmake` until current owners and a deliberate source
   manifest update make the historical driver removable.
10. **Remove redundant current-release prose checks and derived-number pins.**
    Migrate the FST/SDF release wrappers and the v3 integration/profile/
    documentation/release/version checks to required identities and semantic
    consistency. Preserve behavioral closures, documentation, diagnostics,
    licensing, and published manifests.
11. **Replace resource-portability source-token checks with boundary and
    behavioral tests.** Implement the focused resource domains above, prove
    parity for every obligation, and retire the umbrella only when no
    substantive behavior is owned solely by source-token inspection.
12. **Remove the disabled line-budget wrapper; retain meaningful
    translation-unit rules.** Register the translation-unit-structure check
    directly, update its required owners/manifests, and do not introduce a
    physical source-line quota.
13. **Consolidate CMake test registration while preserving assertions,
    arguments, fixtures, labels, and environment.** Preserve GNU-style
    `-UNDEBUG`, the MSVC-style forced assertion header, command arguments,
    properties, and platform-specific environment exactly.
14. **Consolidate repeated test setup and fixture ownership declarations.**
    Use shared registration helpers without changing fixture setup/required
    relationships. Re-run generated-metadata audits, including
    `fsim.ctest-command-uniqueness`.
15. **Complete: retire redundant recursive closure execution bodies; preserve
    standalone execution through one shared runner.** The nine named profiles
    retain their distinct witness sets, direct invocation, transcript checks,
    failure attribution, and no-nested-run behavior; one shared helper owns
    CTest execution and the four language matrices share their witness loop.
16. **Complete: make each CI lane execute its required test set once.** After
    eight approved historical-gate retirements, 415 tests remain registered.
    The former 406-name `-LE` and 173-name `-L` selections overlapped on
    164 fixture witnesses. Each hosted lane now runs one unfiltered CTest
    selection, which emits all 415 names exactly once; the active hosted-lane
    checker rejects additional or filtered CTest invocations.
17. **Complete: replace test/archive count pins with required-test and
    required-content validation.** The Windows install audit now validates
    configured tests against the current required-name file and derives the
    green regression total from CTest JSON. A seven-path required-entry file
    replaces the archive total while permitting additive safe content;
    duplicate/unsafe archive paths remain rejected.
18. **Complete: remove obsolete batch-specific workflow naming while
    retaining all four LLVM lanes.** The four Linux/Windows Debug/Release
    LLVM lanes now use stable `ci-*` retained logs, as do the Windows
    archive/install logs. The active hosted-lane check enforces stable names,
    LLVM/warnings policy, timeout/parallel bounds, and ledger/workflow lane
    agreement.
19. **Complete: verify the gate migration with missing-test,
    missing-package-content, and invalid-input failure probes.** ABI/schema,
    AST-lifetime, fixture-ownership, required-test/ID, source-path, and
    archive-content/path mutations reject for their intended reasons. The
    current-obligations and CTest-command checks compare generated
    registrations read-only; the only metadata addition is the probe test.
    Clean Release/Debug full qualification, hosted monitoring, commit, and
    push remain Change 20 work.
20. **In progress: close Batch 188B without a seven-sample benchmark under
    the user's 2026-09-23 batch-specific waiver.** Clean warnings-as-errors
    Release and Debug builds and their unfiltered 417-test suites pass. The
    command-only performance preflight covers all 76 required combinations;
    no candidate throughput or RSS acceptance result is claimed. After the
    corrected baseline, the only `src/` or `include/` delta is moving the
    identical native-cache schema string into a header for gate ownership.
    Hosted monitoring, commit, push, and handoff remain open.

## Retirement preconditions

A current gate is removable only when all of the following are true:

- every substantive obligation in this inventory has a named registered test
  or focused checker;
- every historical case identity that remains referenced is present exactly
  once;
- replacement positive and negative validator fixtures pass;
- source-package and archive manifests are updated deliberately, not by
  deleting historical evidence;
- `ctest --show-only=json-v1` contains no duplicate test names or commands and
  all required test sets resolve;
- the hosted selector sets are compared after CTest fixture expansion, the
  current 162-test outer overlap is removed, and each
  recursive driver still suppresses nested CTest execution under
  `FSIM_DEDUPLICATED_CTEST`;
- Release test assertions remain enabled, including the GNU-style `-UNDEBUG`
  path;
- no physical source-line quota has been introduced; and
- AST-lifetime, ABI/schema rejection, license/provenance, archive safety, and
  bounded-resource behavior have executable or structurally focused proof.

At Change 2, every proposed helper and replacement named above is still
unimplemented, and no current gate is authorized for removal.
