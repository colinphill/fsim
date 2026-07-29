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
time/event and OR/AND-list `next_trigger`, named-event
notification/replacement/cancellation, strict `notify_delayed`, port updates,
registered primitive-channel update callbacks, and kernel-backed module-local
`sc_signal` objects. Typed port-to-signal bindings enter the same DesignIR
alias graph, including across an HDL/SystemC instance boundary.
Constructor-time native SystemC child members elaborate recursively and may
bind their ports directly to parent signals or ports. Module lifecycle
callbacks execute at deterministic common-kernel boundaries. Standard typed
signal interfaces and exports retain hierarchy metadata while resolving to
common signals. `SC_THREAD` and `SC_CTHREAD` use Boost.Context fibers on the
single simulation thread. General custom-interface metadata is still work in
progress.

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
- `sc_core::sc_event_or_list` and `sc_event_and_list` expressions;
- `sc_core::sc_module`, `sc_module_name`, `sc_sensitive`, and
  `sc_gen_unique_name`;
- `sc_core::sc_interface`, `sc_export<IF>`, `sc_signal<T>`, `sc_in<T>`,
  `sc_out<T>`, and `sc_inout<T>`;
- `sc_core::sc_prim_channel` registration and deduplicated
  `request_update()`;
- positive/negative edge event finders for input and inout sensitivity;
- `SC_MODULE`, `SC_CTOR`, `SC_HAS_PROCESS`, `SC_METHOD`, `SC_THREAD`, and
  `SC_CTHREAD`;
- `sc_dt::sc_logic`, `sc_bv<N>`, `sc_lv<N>`, `sc_uint<N>`, and `sc_int<N>`.

`sc_bv` and `sc_lv` support arbitrary positive compile-time widths.
`sc_uint` and `sc_int` support widths 1 through 64. The implemented datatype
slice includes checked mutable bit selection, binary rendering, vector/integer
construction, width-preserving bitwise and shift operations, reductions, and
wrapping fixed-width integer arithmetic. `sc_logic` and `sc_lv` propagate
four-state unknowns through bitwise operations; conversion of an `sc_lv`
containing `X` or `Z` to an integer is rejected. Signed right shift is
arithmetic, vector shifts insert zeroes, and division or remainder by zero is
rejected. Concatenation/range proxies, mixed-width result typing, arbitrary
precision integer types, and the complete Accellera datatype overload set are
not part of this bounded slice.

Outside an fsim elaboration/process host, `sc_signal` retains a deliberately
local standalone behavior useful for compiling and testing plug-ins. A
module-local `sc_signal` constructed by a registered factory instead attaches
typed value metadata to its primitive-channel handle and enters the common
kernel.

The remaining v1 subset work includes broader named hierarchy, exports,
static and dynamic sensitivity, port binding, channel update semantics, and
event cancellation rules.

## Bidirectional mixed-language hierarchy

SystemC is not a leaf-only integration. A VHDL component/direct instance or a
Verilog/SystemVerilog module instance may bind explicitly to
`systemc:PLUGIN.FACTORY`. In the reverse direction, a SystemC module factory may
register a named foreign-child placeholder and its typed ports during
elaboration; an `fsim.toml` binding for that full instance path then selects a
`vhdl:LIBRARY.ENTITY(ARCHITECTURE)` or `sv:LIBRARY.MODULE` target.

Facade-based modules declare the reverse direction with the fsim extension
`fsim::systemc::hdl_instance`:

```cpp
SC_MODULE(Bridge) {
    sc_core::sc_in<sc_dt::sc_logic> value{"value"};
    sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};
    fsim::systemc::hdl_instance u_hdl{"u_hdl"};

    SC_CTOR(Bridge) {
        u_hdl.bind_input("value", value);
        u_hdl.bind_output("inverted", inverted);
    }
};
```

For a SystemC instance named `top.u_bridge`, the child above has hierarchy path
`top.u_bridge.u_hdl`. A manifest binding for that exact path chooses its VHDL
or Verilog/SystemVerilog implementation. `bind_input`, `bind_output`, and
`bind_inout` infer encoding and width from supported SystemC signals, ports, or
signal-interface exports. The direction is the HDL child's port direction.

Both directions are now elaborated recursively into the same `DesignIR`. The
common elaborator assigns hierarchy/object/process IDs, checks every port and
conversion, detects recursive instantiation, and applies the same explicit
resolver policy. A SystemC parent may therefore contain an HDL child which
contains another bound SystemC child. Any language may be the project top.

Construction parameters are being implemented bidirectionally. Factories can
now register ordered integer, natural, positive, Boolean, or bit parameter
schemas with optional defaults, and module constructors read their canonical
values with `fsim::systemc::construction_value<T>(name)`. Registry construction
validates missing, unknown, duplicate, and subtype-invalid values
transactionally. The common hierarchy walk now evaluates an HDL parameter
override or VHDL generic map in its parent specialization, applies the
parent-language association/name rules, validates the selected schema, and
only then constructs the SystemC module and checks its resulting ports.
Conversely,
`hdl_instance::set_actual(name,
value)` now supplies immutable signed scalar actuals for its manifest-selected
HDL target. The append-only native ABI carries those values into ordinary
VHDL-generic or Verilog/SystemVerilog-parameter specialization and cache
identity, with duplicate/unknown actual diagnostics. Both boundary directions
therefore use immutable canonical construction values in the bounded scalar
subset.
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
any-change/scalar-edge sensitivity, and dynamic `next_trigger` selection for
time, a single event, an OR list, or an AND list. OR waits wake on the first
listed event; AND waits retain progress until every distinct listed event has
occurred. Reads observe committed common-runtime values; writes enter the
common update phase and awaken dependent HDL or SystemC processes in the next
delta. Callback exceptions are contained at the native boundary and poison
only the affected simulation session.

Named `sc_event` objects receive opaque elaboration handles. `notify()` is
immediate, `notify(SC_ZERO_TIME)` enters the next delta, and a non-zero timed
notification enters the future timestamp heap. SystemC time values cross the
native ABI in femtoseconds and must divide exactly by the elaborated global
tick. The common kernel owns the resulting wait lists and event scheduling, so
the interpreter and hybrid LLVM execution paths use identical semantics.
An already-pending delta notification wins. A timed notification is replaced
only by an earlier due time. Immediate notification cancels pending work before
triggering, and `sc_event::cancel()` invalidates a pending delta or timed
notification. Canceled/replaced timestamp-heap entries are generation-checked
no-ops when eventually dequeued. `notify_delayed()` and
`notify_delayed(sc_time)` require no existing pending notification and report
an error otherwise; zero delay targets the next delta and non-zero delay
targets the requested future time.

An `sc_prim_channel` constructed during module elaboration receives a stable
native handle and debug-visible hierarchy record. `request_update()` is
deduplicated until the channel's virtual `update()` callback returns. The
callback runs in stable channel-handle order in the common update phase; a
request for another channel made during that phase is deferred to the next
delta, and a self-request made from `update()` is ignored while the original
request remains pending. Channel callbacks can use registered ports and events
through the same contained native invocation boundary as `SC_METHOD`.

A module-local `sc_signal<T>` becomes a typed, debug-visible common-runtime
signal with its declared initial value. `write()` retains only the last value
requested before the channel update; `read()` continues to return the committed
value until that update completes. A committed change awakens static
sensitivity and dynamic `value_changed_event()` waits in the next delta.
`event()` is true only during that awakened evaluation delta. The internal
signal and its primitive channel share one opaque handle, avoiding a private
SystemC event queue or duplicate value store.

Binding `sc_in<T>`, `sc_out<T>`, or `sc_inout<T>` to a module-local
`sc_signal<T>` registers an elaboration-time alias. The registry requires both
objects to belong to the same module with identical encoding and width.
DesignIR then maps the port handle and channel handle to one dense signal ID.
An HDL parent can therefore drive a bound `sc_in` channel and observe a bound
`sc_out` channel without a copy callback or an extra delta. Input bindings
preserve the parent signal's initial value; output and inout bindings publish
the internal channel's declared initial value. If multiple bound ports connect
one channel to different parent signals, elaboration rejects the design.

An `sc_module` data member constructed with a non-empty `sc_module_name`
registers a native child beneath the module currently under construction. The
constructor scope remains active through all of the child's member
initializers and constructor body, so its ports, channels, events, and
processes receive the child handle. Native children elaborate recursively;
their processes are assigned stable IDs before processes owned directly by
the parent. A child port may bind a type-identical `sc_signal` owned by its
direct parent, producing one common signal ID rather than a copy process or
extra delta. Duplicate child names fail factory construction, and inconsistent
parent/path metadata is rejected during DesignIR elaboration.

The direct-parent binding rule also permits a native child `sc_in<T>`,
`sc_out<T>`, or `sc_inout<T>` to bind its parent's type-identical port. The
child delegates C++ reads or writes through the parent port, while the
elaborator aliases both registered port handles to one common signal ID.
Bindings that skip a hierarchy level are rejected during factory construction.

`sc_signal<T>` implements `sc_signal_in_if<T>`,
`sc_signal_write_if<T>`, and `sc_signal_inout_if<T>`. An
`sc_export<sc_signal_in_if<T>>` or
`sc_export<sc_signal_inout_if<T>>` may bind a compatible signal endpoint, and
one same-typed export may chain through another before a child port binds it.
The append-only host ABI records every supported export and binding edge.
DesignIR preserves the export paths and stable native handles while resolving
the chain to the endpoint's dense signal ID, adding neither storage nor a
scheduler delta. The earlier concrete `sc_export<sc_signal<T>>` spelling
remains source-compatible. Unbound, cyclic, unknown, and conflicting export
chains are rejected. Arbitrary user interfaces in kernel metadata remain
future work.

Every factory root registers `before_end_of_elaboration`,
`end_of_elaboration`, `start_of_simulation`, and `end_of_simulation`. The first
two run after DesignIR object binding; start runs immediately before the first
kernel start; end runs on terminal completion or during teardown of a started
session. Forward phases visit a parent before its native children; end visits
children in reverse order before the parent. Lifecycle state is isolated by
the exact root-handle set in each built project, including warm builds sharing
one loaded plug-in. Exceptions are contained at the C ABI and poison only the
affected build or simulation. Callbacks may mutate ordinary C++ module state
that later processes inspect. Structural registration or binding during these
callbacks, and callback-originated signal transactions, remain unsupported.

The root factory object owns native C++ child members, so native children are
not selected by a separate manifest binding. Crossings from either the root
or a native child into VHDL or Verilog/SystemVerilog continue to require an
explicit foreign-child binding. Dynamic module creation after construction,
non-parent port chains, and arbitrary custom-interface metadata are not yet
implemented.

`SC_THREAD` and `SC_CTHREAD` callbacks retain their ordinary C++ stacks in
Boost.Context 1.91.0 fibers. `wait(sc_time)`, zero-delay wait,
`wait(sc_event)`, OR/AND event-list waits, and plain `wait()` on static
sensitivity yield to the common scheduler. A fiber is a suspension mechanism
only: simulation remains single-threaded and deterministic. Suspended stacks
are explicitly stopped and completed before module destruction or plug-in
unload.

CMake accepts `FSIM_SYSTEMC_FIBER_MODE=AUTO`, `ON`, or `OFF`. `AUTO` and `ON`
use an installed exact Boost.Context 1.91.0 package when present, otherwise
they fetch Boost's official 1.91.0 release archive and verify its published
SHA-256 before building only the x86-64 Context sources. `OFF` retains the
non-fiber developer configuration and emits `FSIM-ELAB-BIND-042` if a design
selects a thread process. Normal Linux builds use the ELF/GAS fcontext backend
and Windows uses PE/MASM; fetched Linux AddressSanitizer builds select ucontext
with Boost's sanitizer fiber-switch hooks.

SystemC work participates in fsim's common phase policy:

- runnable processes execute in stable process-ID order;
- immediate event notifications enqueue active work without recursive calls;
- timed notifications enter the future timestamp heap;
- channel writes become visible in the common update phase; and
- changed channels awaken dependents in the next delta.

The facade and native host callbacks implement dynamic method sensitivity,
port reads, update-phase port writes, named-event notification/cancellation,
OR/AND dynamic event expressions, and primitive-channel registration/update
dispatch. Module-local `sc_signal` values, sensitivities, and event queries use
the common kernel, and typed port-to-signal bindings use common DesignIR
aliases. Native child modules use the same recursive hierarchy and runtime
registry, lifecycle callbacks are root-scoped, and direct-parent port/export
chains resolve to common signals. It does not yet provide dynamic module
construction, arbitrary user-defined channel binding semantics, asynchronous
updates, thread reset/kill controls, or dynamic process creation.

## Deliberately outside v1

- Accellera binary or kernel compatibility
- TLM, AMS, and CCI
- dynamic process creation
- arbitrary custom primitive-channel interfaces and binding semantics
- user replacement of the scheduler
- ARM64 context switching

Models that need these facilities should use an Accellera implementation or a
same-language wrapper around a simpler fsim-compatible boundary.
