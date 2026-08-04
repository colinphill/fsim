<!-- SPDX-License-Identifier: Apache-2.0 -->
# Precompiled-library tutorial

This example separates a reusable SystemVerilog library producer from a local
consumer. The producer exports logical library `vendor` as one relocatable,
read-only `.fsimlib` directory. The consumer maps that directory, searches the
logical library during elaboration, and instantiates `library_child` without
listing or parsing the producer source.

The commands below assume a Debug build at `build/dev`. From the repository
root, first publish the producer library:

```sh
mkdir -p examples/precompiled_library/artifacts
build/dev/fsim build \
  -p examples/precompiled_library/producer/fsim.toml \
  --export-library \
  vendor=examples/precompiled_library/artifacts/vendor.fsimlib
```

The exported directory contains canonical `fsim-library.toml`, checksummed
portable unit and source payloads, and—when the local build supports LLVM—an
optional exact-host native object. Publication refuses to overwrite an
existing destination and makes the completed artifact read-only.

Now build and run the consumer:

```sh
build/dev/fsim check -p examples/precompiled_library/consumer/fsim.toml
build/dev/fsim run   -p examples/precompiled_library/consumer/fsim.toml
```

The consumer manifest maps `vendor` to the artifact and includes `vendor` in
its elaboration search list. Its local `tb` therefore resolves
`tb.u_library_child` to `sv:vendor.library_child`. The run stops at tick 2 and
writes `consumer/precompiled_library.vcd`; the child output is the inverse of
the input throughout the two stimulus intervals.

To demonstrate relocation, move the complete artifact and override the
manifest mapping without editing the project:

```sh
mkdir -p examples/precompiled_library/relocated
mv examples/precompiled_library/artifacts/vendor.fsimlib \
  examples/precompiled_library/relocated/vendor.fsimlib
build/dev/fsim run \
  -p examples/precompiled_library/consumer/fsim.toml \
  --map-library \
  vendor=examples/precompiled_library/relocated/vendor.fsimlib
```

All derived LLVM objects, traces, and other mutable state remain in the
consumer cache or output directory; fsim never writes into the mapped tree.
An optional native payload is admitted only on an exact host/ABI/fingerprint
match. Otherwise fsim restores and compiles the portable unit.

For SystemC libraries, the portable fallback bundles source files and supports
self-contained plug-ins that depend only on fsim/SystemC and standard host
headers. Producer-only include directories, definitions, compiler/linker
options, or external libraries are not a relocatable contract and must not be
used when exporting a portable SystemC library in this format revision.
