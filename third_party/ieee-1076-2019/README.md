<!-- SPDX-License-Identifier: Apache-2.0 -->
# IEEE 1076-2019 package source inventory

This directory contains an unmodified source snapshot from the official
IEEE-P1076 `Packages` repository:

- upstream: `https://gitlab.com/IEEE-P1076/packages.git`
- tag: `1076-2019`
- commit: `16a012320947d378611cc7457f64ed76cb52bac4`
- retrieved for fsim review: 2026-08-02
- license: Apache License 2.0

The upstream `ieee/` and `std/` source directories, `LICENSE`, and
`AUTHORS.md` are retained byte-for-byte. `SHA256SUMS` records every imported
file. `inventory.cmake` supplies fsim's dependency-ordered review and loading
inventory; it does not modify the upstream sources.

These files are the sources of fsim's precompiled standard library artifact
for VHDL-2019: fsim builds the artifact from the `ieee/` packages and from
`std/textio.vhdl` and `std/env.vhdl` at fsim build time. They are also the
originals from which `scripts/derive_ieee_packages.py` derives the per-edition
trees of `third_party/ieee-derived` (VHDL-1987, 1993, 2002 and 2008), which are
the sources of the artifacts of those editions. `std.standard` is
language-predefined rather than parsed as an ordinary source.
