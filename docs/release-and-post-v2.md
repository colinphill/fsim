<!-- SPDX-License-Identifier: Apache-2.0 -->
# Release evidence and post-v2 boundary

This guide separates the supported v2.0 execution contract from release
qualification and from possible post-v2 expansion. It is the current policy
entry point; older batch audit documents remain historical evidence rather
than current platform or resource requirements.

## Current v2.0 execution

fsim runs one deterministic simulation scheduler and one logical time domain.
Elaboration fixes hierarchy ownership before simulation, and safe points order
HDL, SystemC/TLM/SCV, foreign-interface, trace and debugger observations. Build
tools may compile independent translation units in parallel and CTest may run
independent tests concurrently; neither changes simulation semantics or turns
one design into a distributed simulation.

The opaque SystemC backend is worker-ready in a deliberately narrow sense. Its
request/response records are pointer-free, resource-bounded and ordered by
stable identities, and the direct and loopback implementations share the same
codec and failure containment. Current v2 does not automatically partition a
design, start worker processes, place one SystemC kernel per worker, recover a
remote worker, or run a conservative parallel scheduler. The supported path is
the current direct or in-process loopback backend.

## Diagnostics and resource limits

Cataloged `FSIM-*` codes describe current failures. Resource-limit diagnostics
remain errors even when a future execution strategy might provide more memory
or concurrency. A worker transport failure must not be reported as a cache,
source, artifact or language error, and future worker code must preserve the
transactional publication and no-partial-state rules in the current protocol.

Use the [diagnostic catalog](diagnostics.md) for stable code spellings and the
[user/platform guide](user-platform-guide.md) for cache-lock, path, memory and
toolchain triage. Local builds use at least eight workers when the host permits;
every maintained hosted job has a 120-minute timeout. Simulation remains
single-scheduler regardless of build or test parallelism.

## Batch 176 release-candidate boundary

Batch 176 prepares documentation, package definitions, deterministic manifests,
install/uninstall ownership, offline inputs and Debug/fixture smoke evidence.
Changes 1-19 remain one recoverable worktree; Change 20 runs fresh full Debug
builds, regressions and non-Release release-candidate gates, then commits and
pushes once. Batch 176 does not configure, build or test Release, run a
sanitizer, or execute, inspect, restart or monitor hosted CI.

Linux results do not establish Windows behavior. Package definitions may name
the pinned Windows LLVM-MinGW 20260616 UCRT target, but Windows build, warning,
archive and smoke claims require retained Windows evidence. Historical
MSVC/clang-cl performance rows must be mapped to the current target or retired
explicitly; they are not current package claims.

The Windows definition set contains LLVM-MinGW 20260616 UCRT with LLVM disabled
or exact LLVM 22.1.8 enabled. Both records own the verified toolchain URL and
SHA-256, Clang 22.1.8 GNU Windows triple, Tcl, four hosted workers, a 120-minute
limit and Batch 177 warning-log ownership. Project-wide `/bigobj` and the
warning-clean `NDEBUG` assertion policy remain statically checked MSVC-
compatible source contracts; they do not establish an MSVC/clang-cl package or
a Windows execution result.

The Linux package-definition set contains GCC 13 without LLVM, GCC 13 with
exact LLVM 22.1.8, and Clang 22.1.8 with exact LLVM 22.1.8. Batch 176 may retain
Debug package/fixture results for those profiles. Each descriptor assigns its
Release build, archive, install smoke and hosted disposition to Batch 177; the
word `Release` in the supported configuration list is not Batch 176 evidence.
The descriptors also freeze ZIP format, a 120-minute job limit, at least eight
local workers, four hosted workers and unsigned-candidate disposition. Their
registered validator rejects target-set, LLVM-identity and evidence-boundary
drift; it does not manufacture an archive or platform result.

Clean-machine candidate smokes are fixture-backed so each underlying test has
one command owner in a normal regression. The maintained example inventory is
exactly the vertical slice, manifest-free phases, precompiled library, three-
language hierarchy, SDF control and mixed VITAL examples. Staged install tests
use isolated prefixes and share one narrow CTest resource lock because CMake
writes one build-tree `install_manifest.txt`; unrelated tests remain parallel.
Batch 176 records only locally available Debug/fixture results, while Windows
and every Release staged-install smoke remain Batch 177 work.

Compatibility smokes likewise use direct fixture witnesses for official
SystemC 3.0.2/TLM, SCV 2.0.1, DPI-C, VPI and VHPI compilation, installation,
artifacts, cache reconstruction, transactions, ABI/producer rejection and
failure containment. The driver requires the removed-facade path/token
rejection contract before execution. It supplies no legacy SystemC interface
and does not turn local Linux Debug evidence into Windows or Release evidence.

The retained candidate-log audit classifies every local Change 2-16 log and
rejects any unowned warning, error, failed test, assertion, exception, crash,
timeout or kill marker. Expected initial failures remain evidence only when a
specific repaired class and final log are present. Clean checkouts validate the
same policy without requiring ignored local build evidence; release records
carry normalized SHA-256 identities for the locally retained audit.

## Frozen candidate records

`packaging/v2-release-record.txt` is the machine-checked Batch 176 candidate
index. It freezes 1,495 source paths, seven support rows, six maintained
example outputs, nineteen qualification rows, forty-four Linux performance
rows, package-target identities, retained-log audit identity, upstream SBOMs,
licenses/notices and the Debug archive proofs. The candidate is explicitly
unsigned. `packaging/v2-support-matrix.tsv` distinguishes Linux candidate
definitions, definition-only LLVM-MinGW targets and retired MSVC/clang-cl
package targets; it contains no inferred execution.

See the [candidate changelog](changelog-v2.md) and
[known issues](known-issues-v2.md). Any content or count change requires the
source manifest and candidate record to be regenerated and revalidated before
the final Batch 177 qualification.

## Final Batch 177 evidence

Batch 177 owns every Release build, test, qualification and release gate
deferred from Batches 173-176. It also owns fresh sanitizers, the complete
hosted Linux and Windows LLVM-MinGW matrix, CI restart/monitoring and repair,
final Release package/archive/install smokes, and the release tag. All hosted
jobs retain 120-minute limits. A platform row is complete only from its own
retained execution; no Linux result may be copied into a Windows row.

Release evidence must retain the exact command, toolchain and dependency
identity, configuration, manifest or test selection, exit status, elapsed time,
warning/failure disposition and log digest. A green summary cannot override an
error marker, crash, missing expected output or incomplete test inventory.

## Optional post-v2 work

The following are not v2.0 support promises:

- automatic process or machine partitioning, worker launch/recovery and a
  conservative parallel scheduler;
- standalone ahead-of-time application executables beyond the current governed
  object, library and design artifacts;
- additional coverage presentation products beyond the current coverage model,
  database and export contracts; and
- Python bindings, notebook packaging, GUI integration, reverse execution or
  additional host architectures.

Each item requires a separately approved plan, threat/resource model, portable
schema and focused qualification. New work must extend rather than silently
replace the installed `fsim::api`, SystemC/TLM/SCV and foreign-interface ABIs,
current-only artifact regeneration rules, deterministic scheduler semantics or
cataloged diagnostics.
