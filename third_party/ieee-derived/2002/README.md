<!-- SPDX-License-Identifier: Apache-2.0 -->
# IEEE package sources derived for VHDL-2002

These files are included for reference only. fsim does not read them:
it simulates the IEEE packages with its built-in models.

These files are NOT IEEE originals. Each one is the matching file of the
IEEE 1076-2019 package sources (`third_party/ieee-1076-2019`, Apache-2.0)
with the declarations and bodies that later standards added deleted
(`scripts/vhdl_ieee_removals.tsv`, rows for standard 2002) and with the
mechanical rewrites of `scripts/vhdl_ieee_rewrites.tsv` applied, so that
the text is valid VHDL of this standard. No text of an older IEEE original
is used. A generated header names the 2019 file, its SHA-256, the rows
applied, and the notice that applies. `scripts/derive_ieee_packages.py`
writes the files and the `fsim.ieee-derived-*` checks reproduce them byte
for byte.

Packages: math_complex, math_real, numeric_bit, numeric_std, std_logic_1164.

The 2000 text equals the 2002 text for these packages, so `2002` serves
both VHDL-2000 and VHDL-2002.

`SHA256SUMS` records every VHDL file of this directory.
