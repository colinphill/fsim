<!-- SPDX-License-Identifier: Apache-2.0 -->
# v3 API and ABI reference

fsim v3 uses direct v3 schemas and native ABIs. It rejects v2 manifests,
objects, designs, checkpoints, caches, serialized state, and native plug-ins;
there are no compatibility readers or migrations.

## Installed C surfaces

`fsim/api.h` is the stable C11/C++-callable session API. Its structures are
append-only, size-gated prefixes: callers initialize `struct_size` and
`api_version`, and fsim never reads or writes beyond the advertised prefix.
Inputs are copied or validated before publication; returned views retain the
lifetime documented by their owning session.

Foreign-language SDK headers are:

- `fsim/runtime/svdpi.h` for SystemVerilog DPI;
- `fsim/runtime/vpi_user.h` and companion standard VPI headers;
- `fsim/runtime/veriuser.h` and `fsim/runtime/acc_user.h` for IEEE legacy PLI;
- the installed VHPI headers for VHDL;
- `fsim/runtime/tf_plugin_abi.h` for the direct-v3 TF loader.

No C++ exception, STL object, filesystem object, borrowed host pointer, or
transient simulator address crosses a stable C ABI.

## Persisted schemas

Every persisted family carries its own v3 magic, schema, byte order, bounded
lengths, semantic identity, and integrity checks. Decoders validate the entire
candidate before publication and reject unknown schemas, flags, kinds,
noncanonical order, duplicate records, unsafe paths, truncation, trailing
bytes, and resource overflow.

The unified `.fsimcov` database has separate code, SystemVerilog functional,
and PSL namespaces. `.fsimobj`, `.fsimdesign`, `.fsimlib`, checkpoints, runtime
state, and native-cache entries retain the exact HDL profile and governed
dependency provenance needed by their consumer.

## Source-level C++ APIs

Headers below `fsim/app`, `fsim/artifact`, `fsim/frontend`, `fsim/runtime`, and
`fsim/systemc` expose source-level services used by fsim and its tests. They
are tied to the exact source/build and do not promise a stable C++ binary
layout. Installed plug-ins should use the C surfaces above.

See [Unified coverage](code-coverage.md), [IEEE TF and ACC PLI](legacy-pli.md),
[VHDL-2019](vhdl-2019.md), [SystemVerilog-2023](systemverilog-2023.md), and the
[diagnostic catalog](diagnostics.md).
