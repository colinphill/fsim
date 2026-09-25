<!-- SPDX-License-Identifier: Apache-2.0 -->
# Precompiled-library tutorial

This example separates a reusable SystemVerilog library from a local
consumer. A mapped directory owns the `vendor` library catalog and managed
objects. The consumer resolves `library_child` without parsing its old source.

Run from this directory with fsim on `PATH`:

```sh
mkdir -p ../precompiled-library-vendor
fsim library map vendor ../precompiled-library-vendor
fsim compile --library vendor producer/library_child.sv
fsim library objects vendor
fsim compile --library work consumer/tb.sv
fsim elaborate work.tb --search-library vendor --snapshot consumer
fsim simulate --snapshot consumer --trace precompiled_library.vcd
```

The mapping is saved in `.fsim/libraries.toml`. The external directory contains
`library.sqlite3` and fsim-generated artifacts. Create the mapping before
compiling into the target library. Repeating compilation replaces the previous
definitions transactionally; the user does not select artifact filenames.

`tb.u_library_child` resolves to `vendor.library_child`. The simulation stops
at tick 2, and the child output is the inverse of the input during both stimulus
intervals. The snapshot remains usable after the producer source is hidden or
the library is updated.

A second workspace can map the same directory under `vendor` and reuse its
compiled definitions. To relocate the library, stop writers and copy the whole
directory, then change the mapping:

```sh
cp -a ../precompiled-library-vendor ../precompiled-library-relocated
fsim library map vendor ../precompiled-library-relocated
fsim elaborate work.tb --search-library vendor --snapshot relocated
fsim simulate --snapshot relocated
```

Compiled packages work the same way. Compile a package into `vendor`, then
compile its consumer with `--search-library vendor`; the package source need
not remain present. If a provider changes, recompile stale consumers before
elaborating a new snapshot.

`library unmap vendor` preserves the external library. `library delete vendor`
removes the library's managed state and mapping while preserving unrelated
files in that directory. Existing snapshots remain independent. The old
producer and consumer `fsim.toml` files are historical fixtures, not inputs to
this workflow.
