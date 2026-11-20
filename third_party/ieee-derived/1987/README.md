<!-- SPDX-License-Identifier: Apache-2.0 -->
# IEEE package sources derived for VHDL-1987

These files are the sources of fsim's precompiled standard library
artifact for this standard: fsim builds the artifact from them at fsim
build time (the `std` and `ieee` packages listed below).

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

Packages of library `ieee` (`ieee/`): std_logic_1164.

Packages of library `std` (`std/`): textio. The 2019 sources have no
package body for them.

`SHA256SUMS` records every VHDL file of this directory.
