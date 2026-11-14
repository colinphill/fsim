<!-- SPDX-License-Identifier: Apache-2.0 -->
# Synopsys IEEE-library package sources

This directory holds the Synopsys `std_logic_arith`, `std_logic_unsigned`,
`std_logic_signed`, `std_logic_misc`, and `std_logic_textio` packages
(Copyright (c) 1990, 1991, 1992 Synopsys, Inc.), byte-for-byte as shipped in
the GHDL project.

These files are included for reference only. fsim does not read them: it
simulates these packages with its built-in models.

Origin:

- upstream: `https://github.com/ghdl/ghdl`, directory `libraries/synopsys`
- source commit: `9cc54bbc189d28d42aec14e046aec7590ae6bad8` (default branch
  head when retrieved on 2026-11-11; the directory was last changed by
  `f783450ba70ebcad29c1af56d6c96f7a2ae0d3ec`, 2020-02-27)
- files (Git blob IDs match the upstream tree):
  `std_logic_arith.vhdl` (`361ddd142c984701a9e58efed61260d5e4da9471`),
  `std_logic_misc.vhdl` (`409b2a0827be330264f4d2ac62828d30c7a02aeb`),
  `std_logic_misc-body.vhdl` (`84a32bb0970098eee5adf1bce786eb28b92e2804`),
  `std_logic_signed.vhdl` (`27d211be5d08cc11ced3c546370996d420b7920e`),
  `std_logic_textio.vhdl` (`d69a87e37fa1b14abb9f0b29c2db8b0bdc496f5f`),
  `std_logic_unsigned.vhdl` (`3e29847a8228fe738e915a8995931ce4e4cd6277`)

License: the file headers state "This source file may be used and distributed
without restriction provided that this copyright statement is not removed from
the file and that any derivative work contains this copyright notice." The
headers are unchanged in every file; do not remove or edit them. The files are
not covered by fsim's Apache-2.0 license. `SHA256SUMS` records every file.
