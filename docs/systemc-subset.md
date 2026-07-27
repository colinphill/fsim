<!-- SPDX-License-Identifier: Apache-2.0 -->
# SystemC subset and plug-in model

## Compatibility statement

fsim's SystemC support is an IEEE 1666-2023-inspired source subset. It is not
the Accellera kernel and does not claim source completeness, binary
compatibility, or ABI compatibility with an Accellera SystemC installation.

The repository currently supplies the source facade, native plug-in ABI,
dynamic loader, a manifest-driven host compiler with a persistent plug-in
cache, and typed factory construction integrated into common hierarchy
elaboration. A project build collects the configured SystemC sources into one
shared library, loads it, constructs the requested factory instances, and
retains their native objects for the design lifetime. Common-kernel SystemC
execution now covers statically sensitive `SC_METHOD` callbacks, dynamic
time/event `next_trigger`, named-event notification, and port updates.
Event cancellation, internal primitive channels, and fiber-backed thread
execution are still work in progress.

## Source inclusion

Supported code includes either header:

```cpp
#include <systemc>
// or
#include <fsim/systemc.hpp>
```

The forwarding `<systemc>` header resolves to fsim's implementation when the
fsim include directory appears first.

The current facade defines:

- `sc_core::sc_time`, `sc_time_unit`, and `SC_ZERO_TIME`;
- `sc_core::sc_event`, `wait`, and `next_trigger`;
- `sc_core::sc_module`, `sc_module_name`, `sc_sensitive`, and
  `sc_gen_unique_name`;
- `sc_core::sc_interface`, `sc_export<IF>`, `sc_signal<T>`, `sc_in<T>`,
  `sc_out<T>`, and `sc_inout<T>`;
- positive/negative edge event finders for input and inout sensitivity;
- `SC_MODULE`, `SC_CTOR`, `SC_HAS_PROCESS`, `SC_METHOD`, `SC_THREAD`, and
  `SC_CTHREAD`;
- `sc_dt::sc_logic`, `sc_bv<N>`, `sc_lv<N>`, `sc_uint<N>`, and `sc_int<N>`.

`sc_uint` and `sc_int` currently support widths 1 through 64. The facade's
standalone signal operations are useful for compiling and testing plug-ins,
but they do not yet represent complete scheduler/channel semantics.

The v1 subset also requires named hierarchy, exports, static and dynamic
sensitivity, port binding, channel update semantics, event cancellation rules,
and the common fixed-width signed/unsigned operations needed by signal-level
models.

## Bidirectional mixed-language hierarchy

SystemC is not a leaf-only integration. A VHDL component/direct instance or a
Verilog/SystemVerilog module instance may bind explicitly to
`systemc:PLUGIN.FACTORY`. In the reverse direction, a SystemC module factory may
register a named foreign-child placeholder and its typed ports during
elaboration; an `fsim.toml` binding for that full instance path then selects a
`vhdl:LIBRARY.ENTITY(ARCHITECTURE)` or `sv:LIBRARY.MODULE` target.

Both directions are now elaborated recursively into the same `DesignIR`. The
common elaborator assigns hierarchy/object/process IDs, checks every port and
conversion, detects recursive instantiation, and applies the same explicit
resolver policy. A SystemC parent may therefore contain an HDL child which
contains another bound SystemC child. Any language may be the project top.
This covers VHDL→SystemC, SV→SystemC, SystemC→VHDL, and SystemC→SV alongside
the ordinary VHDL↔SV directions; SystemC is a peer hierarchy language rather
than a leaf-only foreign model.

The SystemC-facing ABI exposes this through an elaboration factory plus
append-only host callbacks for registered ports and typed foreign-child
placeholders. Factory, module, port, and foreign-child identities are opaque
64-bit handles; no C++ or internal IR layout crosses the boundary. It does not
permit arbitrary HDL creation after simulation starts. Cross-language binding
is never inferred from a C++ type or unqualified name.

## Plug-in compilation

Each schema-1 manifest may select a C++ compiler and pass include directories,
defines, compile options, link options, and libraries:

```toml
[systemc]
compiler = "clang++"
includes = ["include"]
defines = ["MODEL_REV=2"]
compile_options = ["-O2"]
link_options = []
libraries = []
```

The current driver invokes the selected GCC, Clang, or MSVC executable directly
with an argument vector. It does not concatenate a shell command and does not
perform shell expansion. Source content, compiler arguments and identity,
target/toolchain, ABI version, resolved dependencies, and path-addressed linked
inputs participate in the plug-in cache key. On GCC-like toolchains fsim asks
the compiler to emit each source's complete dependency closure, including
headers found through implicit system include paths, and content-hashes that
closure.

The cache uses process-aware per-key locks, checksum sidecars, atomic
publication, corruption rejection, and stale-lock recovery. If fsim cannot
prove the dependency closure—for example, because an option uses a response
file, an MSVC-only implicit include cannot be resolved conservatively, or a
library is specified only by linker name—the build remains valid but is
deliberately non-cacheable. Changes to a tracked transitive header or linked
file produce a different cache key. Exact uses of the volatile predefined
macros `__DATE__`, `__TIME__`, and `__TIMESTAMP__` in tracked GCC-like inputs
also disable reuse. After a successful compilation, fsim recomputes the plan
and key before publishing the library; if a tracked input changed during the
build, that output is discarded instead of entering the cache.

The current compiler identity covers the resolved driver executable, but does
not yet fingerprint every compiler helper, specifications file, or
environment-injected code-generation setting. The MSVC fallback does not yet
consume compiler-emitted `/sourceDependencies`; its manifest-root scanner
therefore cannot prove compiler-specific include behavior such as
`#pragma include_alias`. Projects relying on those inputs should not treat a
warm MSVC plug-in result as a reproducible cache artifact in this slice.
Non-cacheable plug-in artifact directories are unique and are not yet covered
by automatic eviction.

## Native ABI

A plug-in exports exactly one initialization symbol:

```c
fsim_sc_status_v1 fsim_plugin_init_v1(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar);
```

The two versioned tables use only fixed-width integers, C pointers, callbacks,
and explicit byte views. A plug-in registers module factories through the
registrar. Factories and modules then register ports, processes, sensitivities,
reads, writes, waits, and notifications through the host table.

The integrated-elaboration ABI lets a running typed factory register a
foreign-child placeholder beneath its module handle. The factory registers the
child name and typed port surface, while the manifest—not plug-in C++ code—
selects the HDL implementation. This keeps SystemC-to-HDL hierarchy recursive
and symmetric with HDL-to-SystemC binding without exposing frontend or
`DesignIR` layouts through the native ABI. The earlier untyped factory callback
remains loadable for compatibility but cannot satisfy an integrated hierarchy
binding.

Facade modules register without handwritten factory thunks through
`fsim::systemc::register_module_factory<Module>(host, registrar, name)`.
During construction, named `sc_in`, `sc_out`, and `sc_inout` members register
their typed port handles. After construction, the helper publishes recorded
processes, static sensitivities, edge qualifiers, and `dont_initialize()`
state. The helper passes the host table as factory user context, so separate
loaded sessions do not depend on a process-global host pointer.

Packed ABI values are byte-addressed with least-significant bits first.
`FSIM_SC_BIT2` uses one value plane. The other current encodings use an `aval`
plane followed by an equal-size `bval` plane, preserving `0`, `1`, `X`, and
`Z` without exposing a C++ datatype.

The loader rejects a missing entry point, host/registrar ABI mismatch, or failed
initialization. Factory registrations are buffered until initialization
succeeds, preventing a throwing or failed initializer from partially
registering a plug-in. C++ exceptions are caught at the initialization boundary
and translated into an error; they do not unwind into fsim. The application
build additionally rejects a compiled library that registers no valid module
factory.

## Process execution

The current common-kernel path executes `SC_METHOD` callbacks to completion
with default time-zero initialization or `dont_initialize()`, static
any-change/scalar-edge sensitivity, and dynamic `next_trigger(sc_time)` or
`next_trigger(sc_event)` selection. Reads observe committed common-runtime
values; writes enter the common update phase and awaken dependent HDL or
SystemC processes in the next delta. Callback exceptions are contained at the
native boundary and poison only the affected simulation session.

Named `sc_event` objects receive opaque elaboration handles. `notify()` is
immediate, `notify(SC_ZERO_TIME)` enters the next delta, and a non-zero timed
notification enters the future timestamp heap. SystemC time values cross the
native ABI in femtoseconds and must divide exactly by the elaborated global
tick. The common kernel owns the resulting wait lists and event scheduling, so
the interpreter and hybrid LLVM execution paths use identical semantics.
Multiple pending-notification replacement/cancellation and event
lists/expressions are not implemented yet.
`SC_THREAD` and `SC_CTHREAD` declarations are retained and diagnosed as
non-executable until suspension is implemented with Boost.Context 1.91.0
fibers on x86-64 ELF and Windows PE. A fiber is a suspension mechanism only:
the simulation remains single-threaded and deterministic.

SystemC work participates in fsim's common phase policy:

- runnable processes execute in stable process-ID order;
- immediate event notifications enqueue active work without recursive calls;
- timed notifications enter the future timestamp heap;
- channel writes become visible in the common update phase; and
- changed channels awaken dependents in the next delta.

The facade and native host callbacks implement dynamic method sensitivity,
port reads, update-phase port writes, and named-event notification. It does
not yet provide event cancellation, internal primitive-channel registration,
or fiber suspension.

## Deliberately outside v1

- Accellera binary or kernel compatibility
- TLM, AMS, and CCI
- dynamic process creation
- arbitrary custom primitive channels
- user replacement of the scheduler
- ARM64 context switching

Models that need these facilities should use an Accellera implementation or a
same-language wrapper around a simpler fsim-compatible boundary.
