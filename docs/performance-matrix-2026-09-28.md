<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim and Vivado wall time and memory matrix — September 28, 2026

This report covers all ten original reference workloads. Each table entry is
one uninstrumented cold observation, with a separate complete stimulus
preflight. It is a diagnostic matrix, not the deferred seven-pair statistical
qualification. Small differences between tools do not establish a speedup.

| Case | fsim Wall (s) | Vivado Wall (s) | Vivado / fsim | fsim RSS (MiB) | Vivado RSS (MiB) | Parity |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `original_codec` | 171.613 | 32.817 | 0.19× | 1929.9 | 986.3 | Pass |
| `original_throughput` | 168.080 | 16.917 | 0.10× | 881.6 | 797.6 | Pass |
| `mixed_codec` | 32.933 | 26.916 | 0.82× | 776.5 | 797.6 | Pass |
| `mixed_throughput` | 16.184 | 13.677 | 0.85× | 303.9 | 797.7 | Pass |
| `codex_reference_mode0_frames1` | 10.000 | 10.049 | 1.00× | 203.5 | 797.7 | Pass |
| `codex_reference_mode0_frames2` | 10.051 | 10.350 | 1.03× | 203.5 | 797.6 | Pass |
| `codex_reference_mode1_frames1` | 8.544 | 10.750 | 1.26× | 203.3 | 797.6 | Pass |
| `codex_reference_mode1_frames2` | 8.395 | 10.852 | 1.29× | 203.4 | 797.7 | Pass |
| `codex_throughput_default` | 9.149 | 10.854 | 1.19× | 203.9 | 797.8 | Pass |
| `codex_throughput_direct_syndrome` | 9.348 | 9.647 | 1.03× | 203.6 | 797.7 | Pass |

Wall time includes HDL compilation, elaboration, native setup, simulation,
process launches, and any required launch delay. RSS is the largest GNU Time
`%M` measurement among those phases, converted from KiB to MiB; it is not a
sum of phase peaks or an aggregate live process-tree measurement. Each row has
one observation, so within-row dispersion is unavailable.

## Configuration and parity

Both tools ran with CPU affinity restricted to CPU 0 on an AMD Ryzen AI Max+
395, with matching `LANG=C`, `LC_ALL=C`, `TZ=UTC`, and dependency environment.
Waveforms and interactive debugging were disabled. fsim used LLVM O2 JIT,
one job, and a new workspace and native cache for each run. Vivado 2025.2.1
used `xelab -mt off -debug off`. The host fsim build used Clang 22 Release
and `CMAKE_INTERPROCEDURAL_OPTIMIZATION=ON` (ThinLTO); build parallelism was
12 and is separate from the single-CPU benchmark restriction.

The isolated full-workload testbench copies use
`xorshift32-shift13-17-5-v1`, seed `0x6d2b79f5`; Codex retains its deterministic
patterns. Both tools consumed the same copies. All ten complete canonical
stimulus transcripts matched, and all timed final counts, fingerprints, and
correctness results matched their preflights. The mixed cases use fsim's
explicit `legacy-unprotected-shared-variable` compatibility option; external
design RTL bytes are preserved. All 80 unique historical input paths still
match their recorded bytes.

Both tools ran outside the execution sandbox with matching launch context.
The abandoned first attempt failed during Vivado Tcl startup inside the
sandbox, before HDL execution. No timings from that attempt appear here.

## Phase detail

Native setup and simulation remain combined below because they share the fsim
run command. Instrumented profiles are separate evidence and do not supply
any table timings.

| Case | fsim compile | fsim elaborate | fsim native + simulate | Vivado compile | Vivado elaborate | Vivado simulate |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `original_codec` | 1.075 | 27.912 | 142.625 | 0.824 | 24.196 | 7.796 |
| `original_throughput` | 0.673 | 6.641 | 160.765 | 0.824 | 8.147 | 7.946 |
| `mixed_codec` | 1.345 | 7.292 | 24.294 | 1.597 | 18.477 | 6.841 |
| `mixed_throughput` | 1.243 | 3.632 | 11.307 | 1.596 | 5.739 | 6.340 |
| `codex_reference_mode0_frames1` | 5.049 | 3.027 | 1.922 | 1.588 | 4.381 | 4.079 |
| `codex_reference_mode0_frames2` | 5.149 | 2.977 | 1.922 | 1.889 | 4.430 | 4.029 |
| `codex_reference_mode1_frames1` | 5.048 | 2.074 | 1.420 | 1.588 | 5.132 | 4.029 |
| `codex_reference_mode1_frames2` | 4.949 | 2.073 | 1.371 | 1.639 | 5.032 | 4.180 |
| `codex_throughput_default` | 5.050 | 2.073 | 2.024 | 1.640 | 5.083 | 4.129 |
| `codex_throughput_direct_syndrome` | 5.049 | 1.973 | 2.324 | 1.588 | 3.778 | 4.280 |

All phase values are seconds. Small differences between phase sums and total
Wall are orchestration overhead.

## Regression assessment

The four codec cases remain slower than Vivado. The six Codex observations
are lower, but the mode-0 and direct-syndrome margins are small enough that
this matrix does not establish a statistically reliable advantage.

Against the historical P8A all-ten matrix, the four codec wall observations
fell by 31.8%, 36.6%, 77.2%, and 76.9%, respectively. Three short Codex rows
increased: mode-0/one-frame by 9.5%, mode-0/two-frames by 4.3%, and
direct-syndrome by 9.5%. These are historical single-sample comparisons,
not paired estimates of change.

A separate cold control screen reproduced a small native-phase increase with
unchanged native-object counts and matching fingerprints/correctness:

| Case | Preserved P8A Wall | Current Wall | P8A native + simulate | Current native + simulate |
| --- | ---: | ---: | ---: | ---: |
| Codex mode-0, one frame | 9.395 | 10.048 | 1.220 | 1.872 |
| Codex direct syndrome | 8.942 | 9.948 | 1.571 | 2.123 |

The direct-syndrome control also had 0.454 seconds of compile-time drift.
The source cause of the native delta is unproven. On September 28 the user
accepted these minimal regressions and directed publication and commit
preparation, so no speculative performance repair was applied. The matrix
above retains its original observations and one consistent fsim executable;
control and instrumented profile runs are separate evidence.

The fresh mixed-codec Wall of 32.933 seconds is 0.354 seconds above the latest
retained P96 observation of 32.579 seconds. This does not change the paused
under-15-second target's status.

## Qualification and commit preparation

Cleanup removed a diagnostic-only admission switch, corrected the native
cache schema test from v170 to v178, and repaired the source package manifest
for the intended new source files while excluding generated codebase-index
artifacts. Release passed 454/454 tests after a fresh CMake scan and all-target
build; focused Debug passed 7/7 and Tcl-off passed 6/6, with LLVM enabled.
Independent runtime, semantic, and compiler source audits found no confirmed
correctness blocker. Hosted Windows/CI qualification has not run for this
candidate.

The previous under-15-second mixed-codec optimization loop remains paused.
This matrix does not satisfy that target or the deferred all-ten performance
qualification. Commit and push preparation does not publish a commit.

## Evidence and identity

Evidence root: `build/performance-campaign/matrix-closure-20260928/`.
The source baseline is `codex/v3` at
`220e0c05a03fcf59316bd46495e0b14d6656f4df`, plus the recorded working-tree
patch. `qualified-source.json` and `qualified-source.patch` identify the
qualified matrix source. Per-case manifests identify generated testbenches,
external design sources, executables, dependencies, commands, CPU affinity,
cache state, and environment.

- [Machine-readable rows](../build/performance-campaign/matrix-closure-20260928/round-2/wall-rss-rows.json)
  and [CSV](../build/performance-campaign/matrix-closure-20260928/round-2/wall-rss-rows.csv).
- [Matrix receipt](../build/performance-campaign/matrix-closure-20260928/round-2/matrix-receipt.json).
- Per-case `round-2/<case>/campaign_manifest.json` and `campaign_report.json`;
  `preflight/<case>/{fsim,vivado}/transcript.canonical.json` contains full
  stimulus evidence. `timings/<case>/llvm_o2/sample-01/{fsim,vivado}/engine_result.json`
  contains phase measurements, RSS, correctness, and fingerprints.
- [Matched control receipt](../build/performance-campaign/matrix-closure-20260928/matched-short-control-round1/receipt.json).
- [Qualification logs](../build/performance-campaign/mixed-long-fast-loop/closure-qualification/).

The matrix fsim executable SHA-256 is
`706a4c984ee6f76b5d266b56861daede47da69856c0de29643422605388de8d3`.
The preserved P8A control SHA-256 is
`ba2d29bfa09edd0a9d637a32b0e6d9afe28674a537bebc640346cfa5df53a741`.
Later report/checkpoint/manifest edits do not change the measured executable.
The complete dependency hashes are recorded in each campaign manifest.

The frozen `d11004e4` historical campaign and its separately labeled
compatibility repair remain historical evidence, not the identity of the
current candidate executable.
