<!-- SPDX-License-Identifier: Apache-2.0 -->
# Pending sampled-value witnesses

The verified named-event witness has been promoted to
[`../named_event_triggered_nba.sv`](../named_event_triggered_nba.sv), with its
oracle transcript at
[`../expected/named_event_triggered_nba.out`](../expected/named_event_triggered_nba.out).
It is part of the permanent thirteen-case Vivado and fsim scheduling corpus.

`sampled_mixed_active_reactive.sv` drives one clock into a module Active
consumer and a program Reactive consumer. Its module call to `$past` is
conditional and skipped at the first edge; the program calls `$past` only at
the second edge. The source-predicted program result is still the t1 sampled
value, `0`, as recorded in `sampled_mixed_active_reactive.out`. Vivado 2025.2.1
instead prints `past=1` for the program and `past=0` for the module. This is
retained in `sampled_mixed_active_reactive.vivado-2025.2.1.out`. The mismatch
is consistent with a sampled-history limitation, but does not establish how
Vivado implements history capture; the Vivado result is not the predicted
transcript.

`sampled_mixed_active_reactive_native.sv` repeats this stimulus with a
continuous assignment driving the clock net. Its `.out` is a source-predicted
transcript awaiting the external Vivado oracle. After that oracle is recorded,
include this case in interpreter, LLVM O0, and LLVM O2 witness runs and require
positive native-process admission for the compiled runs.

The runtime pre-registers expanded clocked sampled-value reads in every
process program present at `start()`, including read instructions in static
fork branch bodies. A process program introduced later through a dynamic
clone or runtime-created process is not added to this capture index; this
change does not claim coverage for those later-created programs.

`sampled_repeated_zero_time.sv` creates two positive edges at one timestamp
using nested `#0` updates and then checks `$past(data, 2, , @(posedge clock))`.
IEEE 1800-2023 §16.5 leaves behavior undefined when a clocking event
transitions more than once in one time step; §16.9.3 defines `$past` using
strictly prior qualifying time steps. The corresponding 2023 sampled-value
clauses were verified directly. The public [IEEE Std 1800-2017
text](https://rfsoc.mit.edu/6S965/_static/F24/documentation/1800-2017.pdf)
provides an openly accessible supporting reference for the same rules. This
witness is diagnostic only and has no normative `.out`; Vivado 2025.2.1's
transcript is preserved separately in
`sampled_repeated_zero_time.vivado-2025.2.1.out`.

The predicted mixed transcript is unverified source-based evidence; the two
Vivado transcripts record externally observed output. None of these files is
part of the current qualification gate. For each top, run in a fresh
directory with the installed Vivado simulator:

```sh
xvlog --sv -work work /path/to/<fixture>.sv
xelab -mt off -debug off <top> -s <top>_snapshot
xsim <top>_snapshot -R
```

Do not promote the mixed witness until its history-capture behavior is
resolved. Keep the repeated-edge witness diagnostic-only; it must not define
normative behavior.
