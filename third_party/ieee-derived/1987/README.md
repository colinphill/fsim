<!-- SPDX-License-Identifier: Apache-2.0 -->
# IEEE package sources derived for VHDL-1987

These files are included for reference only. fsim does not read them:
it simulates the IEEE packages with its built-in models.

These files are NOT IEEE originals. Each one is the matching file of the
IEEE 1076-2019 package sources (`third_party/ieee-1076-2019`, Apache-2.0)
with the declarations and bodies that later standards added deleted
(`scripts/vhdl_ieee_removals.tsv`, rows for standard 1987) and with the
mechanical rewrites of `scripts/vhdl_ieee_rewrites.tsv` applied, so that
the text is valid VHDL of this standard. No text of an older IEEE original
is used. A generated header names the 2019 file, its SHA-256, the rows
applied, and the notice that applies. `scripts/derive_ieee_packages.py`
writes the files and the `fsim.ieee-derived-*` checks reproduce them byte
for byte.

Packages: std_logic_1164.

`SHA256SUMS` records every VHDL file of this directory.
