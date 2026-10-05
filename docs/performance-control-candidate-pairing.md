# Throughput control/candidate pairing runner

`python3 scripts/perf_pair_campaign.py` is the control/candidate wrapper for the
existing Cache252 throughput fixtures. It is deliberately separate from
`scripts/perf_campaign.py`, whose `--samples` still means fsim/Vivado pairs.

Example after the control and candidate executables have been frozen:

```sh
python3 scripts/perf_pair_campaign.py \
  --control /absolute/path/to/frozen-control/fsim \
  --candidate /absolute/path/to/frozen-candidate/fsim \
  --control-provenance 'control commit/build identity' \
  --candidate-provenance 'candidate commit/build identity' \
  --output /absolute/path/to/new-empty-evidence-directory \
  --pairs 7 \
  --seed 0x6d2b79f5 \
  --cpu 3
```

The runner always selects `llvm_o2`, `original_throughput`, and
`mixed_throughput`. `--pairs 5` is the minimum decision run; `--pairs 7` or
more is required for final qualification. Supply identical `--root NAME=PATH`
overrides for every source root when the manifest defaults do not identify the
intended trees. The runner requires an explicit nonzero CPU already in the
process affinity, pins each subprocess to that CPU, and records the host
identity. Choose an idle CPU for the measurement window; the runner does not
change host load or affinity.

Before Wall timing, the runner invokes `perf_campaign.py` once for the control
with `--prepare-full --preflight-only`, then invokes it for the candidate with
the exact generated control overlays. Each executable is independently
checked against Vivado. Timing starts only if the control, candidate, and both
Vivado preflight transcripts, correctness lines, fixture bytes, source hashes,
stimulus seed/algorithm, tool identities, and host configuration agree. The
existing full-workload verifier must identify both cases as original workload
fixtures. The mixed-language case uses the existing explicit
`legacy-unprotected-shared-variable` compatibility profile in both preflight
and timing commands. The preflight records retain canonical transcript JSON
and hashes.

Every timed control/candidate leg uses `perf_campaign.run_engine` in a unique
workspace, so fsim's native cache begins empty for each leg. The runner never
drops the OS page cache; the preflight and alternating execution order leave
the shared OS cache warm. Profiling is not part of this command. Per-leg phase,
RSS, native-cache, and transcript records remain under the output directory.

Rounds randomize the order of the two cases and independently balance
control-first versus candidate-first within each case. The report pairs legs
by explicit `round-NN:<case>` ID. It reports per-leg median/IQR phase and RSS,
paired median candidate/control Wall ratio and delta, and deterministic paired
bootstrap intervals. A point estimate alone is not a supported regression;
overlapping paired intervals remain inconclusive. Absolute qualification is
separate from the control/candidate improvement inference. It requires at
least seven complete pairs in both cases, canonical preflight parity, verified
full workloads, candidate median Wall at or below 15 seconds in both cases,
original median at or below 1.05 times candidate mixed median, and an
anti-slowdown guard requiring original median at or below 1.05 times the lower
of control and candidate mixed medians. The report shows per-case point
estimates and paired confidence intervals independently, so absolute targets
can pass while relative improvement remains inconclusive. Missing, incomplete,
identity-mismatched, or incorrect runs cannot qualify.

The focused Python contract test is registered as
`fsim.performance-pair-campaign`.
