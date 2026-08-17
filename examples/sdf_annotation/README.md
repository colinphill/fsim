<!-- SPDX-License-Identifier: Apache-2.0 -->
# SDF annotation control example

This example shows the current public SDF control surfaces without duplicating
a standard-cell corpus. Replace `cells.sdf`, `fsim.toml`, the root and cell
glob with the identities from your design.

Project CLI:

```sh
fsim run --project fsim.toml --sdf cells.sdf --sdf-root tb \
  --sdf-cell 'tb.dut.*' --delay-mode max --sdf-report-limit 256
```

Manifest-free artifact phases use the same annotation request:

```sh
fsim elaborate --object design.fsimobj --top sv:work.tb \
  --output timed.fsimdesign --sdf cells.sdf --sdf-root tb --delay-mode typ
fsim simulate --design timed.fsimdesign --sdf cells.sdf --sdf-root tb
```

The sibling [`annotate.tcl`](annotate.tcl) script demonstrates transactional
Tcl configuration and bounded summary/report access:

```sh
fsim tcl --project fsim.toml annotate.tcl cells.sdf tb 'tb.dut.*'
```

For native integrations, populate `fsim_sdf_input_t` and
`fsim_sdf_options_t`, call `fsim_session_configure_sdf`, then read
`fsim_session_get_sdf_summary` and indexed
`fsim_session_get_sdf_report_entry` results. Every structure must advertise
its prefix size and `FSIM_API_VERSION`; see `include/fsim/api.h`.

The control layer is bounded and transactional. A rejected selector, duplicate,
phase transition or report limit leaves the previously published request
unchanged. Use the sibling
[mixed VHDL/VITAL example](../sdf_vital_mixed/README.md) for a clean-room timing
cell and mixed-language target.
