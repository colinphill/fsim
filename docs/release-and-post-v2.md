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

## v2.0.0 final release freeze

Batch 177 has completed local Clang/GCC Debug, Release, exact-LLVM,
LLVM-disabled, ASan/UBSan, deterministic archive, installed-consumer and
supply-chain qualification. The compiled version and package identity are
`2.0.0`; archives and the annotated tag use the explicit `unsigned-release`
disposition. The final source and three Linux archive byte identities are
refreshed after documentation freeze and again after the Change 20 clean
builds.

The latest completed hosted run at the pre-release base was audited before the
release commit. Its Linux contract failures and Windows SCV CRLF materialization
failure are repaired locally. The nine pinned hosted lanes still run only from
the sole pushed release commit. A Windows row is complete only from its own
LLVM-MinGW result, and the annotated tag is not published until that matrix is
green.

## Historical Batch 176 release-candidate boundary

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

## Frozen release records

`packaging/v2-release-record.txt` is the machine-checked v2.0.0 release index.
It freezes 1,498 source paths, seven support rows, six maintained
example outputs, nineteen qualification rows, forty-four Linux performance
rows, package-target identities, retained-log audit identity, upstream SBOMs,
licenses/notices and the retained qualification proofs. The release is
explicitly unsigned. `packaging/v2-support-matrix.tsv` distinguishes Linux
definitions, definition-only LLVM-MinGW targets and retired MSVC/clang-cl
package targets; it contains no inferred execution.

See the [release notes](changelog-v2.md) and
[known issues](known-issues-v2.md). Any content or count change requires the
source manifest and release record to be regenerated and revalidated before
publication.

## Batch 177 execution evidence

Batch 177 completes every Release build, test, qualification and release gate
deferred from Batches 173-176. Local sanitizer and Release/package/install
lanes are complete. The hosted Linux and Windows LLVM-MinGW matrix, final
post-push archive capture and release tag remain Change 20 operations. All
hosted jobs retain 120-minute limits. A platform row is complete only from its
own retained execution; no Linux result may be copied into a Windows row.

Release evidence must retain the exact command, toolchain and dependency
identity, configuration, manifest or test selection, exit status, elapsed time,
warning/failure disposition and log digest. A green summary cannot override an
error marker, crash, missing expected output or incomplete test inventory.

The following execution matrix is frozen before any Batch 177 Release,
sanitizer, archive or hosted lane begins. Changes 1-19 remain uncommitted, so a
hosted or Windows-only lane cannot qualify their content before Change 20's
single push. Such lanes retain their original functional change ownership but
use the explicit `post-push-hosted-change20` completion boundary.

```text
id	change	state	platform	toolchain	configuration	workers	timeout_minutes	action	artifact	retained_log	completion_boundary
E177-01	B177-C05	planned	linux-x86_64	clang-22.1.8+llvm-22.1.8	Debug	8	120	clean-first-build	build/llvm22-ninja-debug	build/qualification/batch177-change05-clang-debug-build.log	local-change05
E177-02	B177-C05	planned	linux-x86_64	clang-22.1.8+llvm-22.1.8	Debug	8	120	full-regression-and-execution-smokes	build/llvm22-ninja-debug	build/qualification/batch177-change05-clang-debug-regression.log	local-change05
E177-03	B177-C06	planned	linux-x86_64	clang-22.1.8+llvm-22.1.8	Release	8	120	clean-first-build	build/clang22-llvm22-ninja-release	build/qualification/batch177-change06-clang-release-build.log	local-change06
E177-04	B177-C06	planned	linux-x86_64	clang-22.1.8+llvm-22.1.8	Release	8	120	full-regression-and-release-gates	build/clang22-llvm22-ninja-release	build/qualification/batch177-change06-clang-release-regression.log	local-change06
E177-05	B177-C07	planned	linux-x86_64	gcc-13.3+llvm-22.1.8	Release	8	120	clean-first-build	build/gcc-release	build/qualification/batch177-change07-gcc-llvm-release-build.log	local-change07
E177-06	B177-C07	planned	linux-x86_64	gcc-13.3+llvm-22.1.8	Release	8	120	full-regression-and-package-smoke	build/gcc-release	build/qualification/batch177-change07-gcc-llvm-release-regression.log	local-change07
E177-07	B177-C07	planned	linux-x86_64	gcc-13.3-no-llvm	Release	8	120	clean-first-build	build/ci-linux-release	build/qualification/batch177-change07-gcc-no-llvm-release-build.log	local-change07
E177-08	B177-C07	planned	linux-x86_64	gcc-13.3-no-llvm	Release	8	120	full-regression-and-package-smoke	build/ci-linux-release	build/qualification/batch177-change07-gcc-no-llvm-release-regression.log	local-change07
E177-09	B177-C07	planned	linux-x86_64	gcc-13.3-asan-ubsan	Debug	8	120	full-sanitizer-regression	build/ci-sanitizers	build/qualification/batch177-change07-gcc-asan-ubsan.log	local-change07
E177-10	B177-C08	planned	linux-x86_64	github-ubuntu-gcc-no-llvm	Debug	4	120	hosted-build-test-closure	github-linux-gcc-debug	build/qualification/batch177-change20-hosted-linux-gcc-debug.log	post-push-hosted-change20
E177-11	B177-C08	planned	linux-x86_64	github-ubuntu-gcc-no-llvm	Release	4	120	hosted-build-test	github-linux-gcc-release	build/qualification/batch177-change20-hosted-linux-gcc-release.log	post-push-hosted-change20
E177-12	B177-C08	planned	linux-x86_64	github-ubuntu-gcc+llvm-22.1.8	Debug	4	120	hosted-build-test	github-linux-llvm22-debug	build/qualification/batch177-change20-hosted-linux-llvm22-debug.log	post-push-hosted-change20
E177-13	B177-C08	planned	linux-x86_64	github-ubuntu-gcc+llvm-22.1.8	Release	4	120	hosted-build-test	github-linux-llvm22-release	build/qualification/batch177-change20-hosted-linux-llvm22-release.log	post-push-hosted-change20
E177-14	B177-C08	planned	linux-x86_64	github-ubuntu-clang-fuzz	RelWithDebInfo	4	120	hosted-frontend-fuzz-smoke	github-linux-fuzz	build/qualification/batch177-change20-hosted-linux-fuzz.log	post-push-hosted-change20
E177-15	B177-C08	planned	windows-x86_64	llvm-mingw-20260616-no-llvm	Debug	4	120	hosted-build-test	github-windows-debug-llvm-off	build/qualification/batch177-change20-hosted-windows-debug-llvm-off.log	post-push-hosted-change20
E177-16	B177-C08	planned	windows-x86_64	llvm-mingw-20260616+llvm-22.1.8	Debug	4	120	hosted-build-test	github-windows-debug-llvm-on	build/qualification/batch177-change20-hosted-windows-debug-llvm-on.log	post-push-hosted-change20
E177-17	B177-C08	planned	windows-x86_64	llvm-mingw-20260616-no-llvm	Release	4	120	hosted-build-test	github-windows-release-llvm-off	build/qualification/batch177-change20-hosted-windows-release-llvm-off.log	post-push-hosted-change20
E177-18	B177-C08	planned	windows-x86_64	llvm-mingw-20260616+llvm-22.1.8	Release	4	120	hosted-build-test	github-windows-release-llvm-on	build/qualification/batch177-change20-hosted-windows-release-llvm-on.log	post-push-hosted-change20
E177-19	B177-C09	planned	all	source-manifest-v1	all	1	120	deterministic-source-archive	build/release/fsim-v2.0.0-source.zip	build/qualification/batch177-change09-source-archive.log	local-change09
E177-20	B177-C10	planned	linux-x86_64	clang-22.1.8+llvm-22.1.8	Release	1	120	deterministic-binary-archive	build/release/fsim-v2.0.0-linux-x86_64-clang22-llvm22.zip	build/qualification/batch177-change10-linux-clang22-llvm22-archive.log	local-change10
E177-21	B177-C10	planned	linux-x86_64	gcc-13.3+llvm-22.1.8	Release	1	120	deterministic-binary-archive	build/release/fsim-v2.0.0-linux-x86_64-gcc13-llvm22.zip	build/qualification/batch177-change10-linux-gcc13-llvm22-archive.log	local-change10
E177-22	B177-C10	planned	linux-x86_64	gcc-13.3-no-llvm	Release	1	120	deterministic-binary-archive	build/release/fsim-v2.0.0-linux-x86_64-gcc13-no-llvm.zip	build/qualification/batch177-change10-linux-gcc13-no-llvm-archive.log	local-change10
E177-23	B177-C10	planned	windows-x86_64	llvm-mingw-20260616-no-llvm	Release	4	120	deterministic-binary-archive	fsim-v2.0.0-windows-x86_64-llvm-mingw-no-llvm.zip	build/qualification/batch177-change20-windows-no-llvm-archive.log	post-push-hosted-change20
E177-24	B177-C10	planned	windows-x86_64	llvm-mingw-20260616+llvm-22.1.8	Release	4	120	deterministic-binary-archive	fsim-v2.0.0-windows-x86_64-llvm-mingw-llvm22.zip	build/qualification/batch177-change20-windows-llvm22-archive.log	post-push-hosted-change20
E177-25	B177-C11	planned	all	supply-chain-record-v1	all	1	120	sbom-license-notice-signature-audit	packaging/v2-release-record.txt	build/qualification/batch177-change11-supply-chain.log	local-change11
E177-26	B177-C12	planned	linux-x86_64	clang-22.1.8+llvm-22.1.8	Release	8	120	install-discovery-example-uninstall-reproducibility	build/release/fsim-v2.0.0-linux-x86_64-clang22-llvm22.zip	build/qualification/batch177-change12-linux-clang22-llvm22-install.log	local-change12
E177-27	B177-C12	planned	linux-x86_64	gcc-13.3+llvm-22.1.8	Release	8	120	install-discovery-example-uninstall-reproducibility	build/release/fsim-v2.0.0-linux-x86_64-gcc13-llvm22.zip	build/qualification/batch177-change12-linux-gcc13-llvm22-install.log	local-change12
E177-28	B177-C12	planned	linux-x86_64	gcc-13.3-no-llvm	Release	8	120	install-discovery-example-uninstall-reproducibility	build/release/fsim-v2.0.0-linux-x86_64-gcc13-no-llvm.zip	build/qualification/batch177-change12-linux-gcc13-no-llvm-install.log	local-change12
E177-29	B177-C12	planned	windows-x86_64	llvm-mingw-20260616-no-llvm	Release	4	120	install-discovery-example-uninstall-reproducibility	fsim-v2.0.0-windows-x86_64-llvm-mingw-no-llvm.zip	build/qualification/batch177-change20-windows-no-llvm-install.log	post-push-hosted-change20
E177-30	B177-C12	planned	windows-x86_64	llvm-mingw-20260616+llvm-22.1.8	Release	4	120	install-discovery-example-uninstall-reproducibility	fsim-v2.0.0-windows-x86_64-llvm-mingw-llvm22.zip	build/qualification/batch177-change20-windows-llvm22-install.log	post-push-hosted-change20
```

The normalized thirty-row matrix has SHA-256
`e02bdc6d451800585fe54fdfd75e8c050cfb7673e6225377f414eafa6e3f9f79`.

The hosted workflow statically maps rows `E177-10` through `E177-18` one-to-one
to nine unique artifact and retained-log identities. Each lane creates its log
before dependency setup, appends toolchain, configure, build and test or fuzz
output with native command failures preserved, and uploads the log through its
own matrix-expanded `actions/upload-artifact@v7` owner under `always()` with
missing-file rejection. This prepares evidence capture only; the nine hosted
lanes remain unexecuted until the post-push Change 20 boundary.

The source-package lane publishes `build/release/fsim-v2.0.0-source.zip` with
root `fsim-v2.0.0-source`. Its checked policy requires two independently staged
byte-identical ZIPs, exact manifest order and normalized metadata, preflighted
safe paths before extraction, unsafe-manifest rejection, extracted-manifest
reconstruction and a configure with FetchContent fully disconnected. Any later
source change invalidates the release bytes and requires regeneration at the
final freeze; a prior Change 9 digest is evidence for the mechanism, not
permission to reuse stale release content.

The binary-package policy fixes five final archive names. All three local Linux
Release targets use binary-only deterministic staging with exact package roots,
normalized metadata, repeated byte identity and safe path/relative-symlink
preflight. The two LLVM-MinGW Release workflow entries carry their own package,
archive and log identities; after successful hosted tests they execute the same
binary-only owner and conditionally upload the ZIP plus retained archive log.
Those Windows archives remain absent until the post-push Change 20 execution,
and no Linux archive may substitute for either one.

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
