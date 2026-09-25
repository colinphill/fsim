<!-- SPDX-License-Identifier: Apache-2.0 -->
# SDF annotation control example

This example shows the current public SDF controls without duplicating a
standard-cell corpus. Substitute your HDL sources, `cells.sdf`, top, and cell
selector for the example names.

Compile into the workspace and elaborate a timed snapshot:

```sh
fsim compile --library work cells.sv tb.sv
fsim elaborate work.tb --snapshot timed --sdf cells.sdf --sdf-root tb \
  --sdf-cell 'tb.dut.*' --delay-mode max --sdf-report-limit 256
fsim simulate --snapshot timed
```

The same annotation request is available before snapshot simulation:

```sh
fsim simulate --snapshot timed --sdf cells.sdf --sdf-root tb
```

The sibling [`annotate.tcl`](annotate.tcl) script demonstrates transactional
Tcl configuration and bounded summary/report access:

```sh
fsim tcl --snapshot timed annotate.tcl cells.sdf tb 'tb.dut.*'
```

For native integrations, populate `fsim_sdf_input_t` and
`fsim_sdf_options_t`, call `fsim_session_configure_sdf`, then read
`fsim_session_get_sdf_summary` and indexed
`fsim_session_get_sdf_report_entry` results. Every structure advertises its
prefix size and `FSIM_API_VERSION`; see `include/fsim/api.h`.

The control layer is bounded and transactional. A rejected selector, duplicate,
phase transition, or report limit preserves the previously published request.
See the [mixed VHDL/VITAL example](../sdf_vital_mixed/README.md) for a timing
cell and mixed-language target.
