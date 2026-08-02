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

Bundling a file does not by itself claim executable support. The feature
matrix identifies the reviewed profiles that fsim analyzes and executes. In
particular, `std.standard` is language-predefined rather than parsed as an
ordinary source, and packages remain inactive until their Batch 120 review
stage is complete.
