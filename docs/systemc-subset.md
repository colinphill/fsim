<!-- SPDX-License-Identifier: Apache-2.0 -->
# SystemC integration and plug-in model

## Compatibility statement

fsim executes SystemC models with the official Accellera SystemC kernel. The
former fsim-defined SystemC facade, Boost.Context scheduler, custom event and
primitive-channel execution callbacks, and legacy foreign-child interface are
not supported. SystemC processes, events, signals, channels, lifecycle hooks,
and TLM activity retain their Accellera semantics.

The fsim boundary is a versioned C ABI used to construct model roots, expose
typed HDL-facing ports, and synchronize the Accellera kernel with the common
mixed-language scheduler. It is not the Accellera library ABI and does not make
independently built SystemC binaries portable between SystemC installations or
toolchains. fsim compiles configured model sources against its selected
Accellera build and loads the resulting shared library.

## Source model

SystemC sources include the official header and fsim's bridge helpers:

```cpp
#include <systemc>
#include <fsim/systemc/accellera.hpp>
```

`<fsim/systemc.hpp>` remains a convenience include for this same official
integration. It does not provide a separate kernel or datatype implementation.

Models use ordinary Accellera constructs, including `SC_METHOD`, `SC_THREAD`,
`SC_CTHREAD`, `sc_event`, `sc_signal`, `sc_prim_channel`, standard ports and
exports, lifecycle callbacks, datatypes, TLM-1, and TLM-2. Native child modules
and objects remain owned by the Accellera hierarchy. fsim inventories that
hierarchy after construction and maps supported boundary ports to DesignIR;
it does not reimplement internal SystemC objects in the common kernel.

Factories are exported declaratively:

```cpp
SC_MODULE(Bridge) {
    sc_core::sc_in<sc_dt::sc_logic> value{"value"};
    sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};

    SC_CTOR(Bridge) {
        SC_METHOD(evaluate);
        sensitive << value;
    }

    void evaluate() {
        inverted.write(~value.read());
    }
};

SC_FSIM_EXPORT_AS(Bridge, "bridge");
```

Any number of `SC_FSIM_EXPORT` or `SC_FSIM_EXPORT_AS` declarations may appear
across one model image. Public names are sorted before registration, duplicate
names reject the whole registration transaction, and distinct aliases of one
module type are permitted. A factory may expose ordered construction
parameters with `make_factory_parameters`; constructors read their canonical
values with `construction_value<T>`.

## Mixed-language hierarchy

A VHDL, Verilog, or SystemVerilog instance may select a SystemC factory with a
`systemc:PLUGIN.FACTORY` manifest binding. Ports use ordinary official
Accellera typed construction and binding syntax. SystemC source does not
declare HDL proxy children; compose such children in the owning HDL hierarchy.

The bridge validates direction, encoding, width, hierarchy ownership, and
complete binding before simulation. Supported port, signal-interface, and
export chains resolve to one DesignIR signal at the HDL boundary. SystemC
internals remain native Accellera objects. Construction parameters and HDL
generic/parameter actuals are immutable inputs to elaboration and artifact
identity. Dynamic cross-language hierarchy creation after simulation starts is
not supported.

## Kernel synchronization

Each SystemC root owns an Accellera simulation context. Before entering the
kernel, fsim publishes current HDL boundary inputs. The bridge advances the
native kernel to the common scheduler's current femtosecond time, lets native
delta cycles settle, publishes changed boundary outputs, and reports the next
native timed activity. The common scheduler then waits for either an HDL input
change or that native activity.

`SC_THREAD` and `SC_CTHREAD` suspension therefore uses the Accellera process
implementation. `wait`, `next_trigger`, notification replacement and
cancellation, channel update ordering, `sc_signal` update semantics, and
lifecycle ordering are not translated into fsim-defined callbacks. Immediate
event notification during the Accellera update phase remains illegal; models
must request a delta notification with `notify(SC_ZERO_TIME)` there.

Interpreter and LLVM execution share the same bridge. Debugger, trace, C/C++,
CLI, Tcl, object, design, mapped-library, and checkpoint surfaces observe
stable boundary identities while transient Accellera object addresses and
callbacks are rebound for each fresh session.

## Plug-in compilation and cache

Each schema-1 manifest may select a C++ compiler and pass include directories,
definitions, compile options, link options, and libraries:

```toml
[systemc]
compiler = "clang++"
includes = ["include"]
defines = ["MODEL_REV=2"]
compile_options = ["-O2"]
link_options = []
libraries = []
```

The driver invokes GCC, Clang, MSVC, or clang-cl directly with an argument
vector. Its fingerprint includes ordered source content, dependency closure,
options, linked inputs, compiler identity and environment, target and shared
library format, C++ mode, CRT, fsim ABI, and the selected Accellera runtime.
GCC-like dependency files and MSVC `/sourceDependencies` JSON extend the cache
identity to transitive headers and supported compiled-header inputs. Inputs
that cannot be modeled safely make the build deliberately non-cacheable.

The cache publishes a versioned key, size, and SHA-256 record under a per-key
process lock. The shared library is installed before metadata becomes the
commit point. Missing, corrupt, stale, incomplete, or incompatible pairs are
cache misses and abandoned staging state is repaired by the locked writer.

On Windows, generated plug-ins match fsim's CRT configuration, use explicit
object/PDB/import/export outputs, UTF-16 response files, and safe DLL search.
No context-switching assembly or fiber backend participates in compilation or
cache identity.

## Native boundary

A model image exposes one initialization symbol:

```c
fsim_sc_status_v1 fsim_plugin_init_v1(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar);
```

ABI version 3 uses fixed-width integers, C pointers, callbacks, and explicit
byte views. The registrar publishes typed factory schemas. During construction,
the official bridge registers root modules and boundary ports and returns
stable opaque handles. At runtime, the host supplies boundary reads and writes,
the current femtosecond time, and the wait-for-input-or-native-activity hook.
There are no host callbacks for custom processes, events, waits,
notifications, primitive-channel updates, or legacy foreign children.

Registration is buffered and validated before factories or image ownership
become visible. Missing symbols, ABI mismatch, malformed schemas, construction
failure, and escaped callbacks reject the transaction without partial
publication. Native exceptions are contained at the boundary. Loaded images
and Accellera roots stay alive for the owning built project and are torn down
after the native kernel finishes.

## Native TLM and observation

TLM-1 FIFOs, blocking and nonblocking put/get/peek, transport and analysis, and
TLM-2 initiator/target sockets, generic payloads, phases, DMI, debug transport,
extensions and quantum keeping execute natively inside one Accellera island.
Only an explicit bridge serializes a transaction. Stable endpoint, peer,
sequence and transaction identities correlate TLM activity with debugger and
trace time/delta records without presenting it as a signal change.

After binding, fsim freezes a bounded inventory of native signals, buffers,
clocks, resolved channels, ports, exports, hierarchical aliases and supported
custom-channel metadata. Post-update dirty hooks feed VCD and FST without
polling paths. Debug reads/writes and custom-channel adapters run only at safe
points; disabled observation performs no value capture, and bounded queues use
explicit retryable backpressure.

## Backend and artifacts

The in-process backend and serialized loopback exchange the same pointer-free,
bounded protocol. Messages retain stable island, hierarchy, object, endpoint,
transaction and sequence identities plus exact femtosecond time, delta and
region. Disconnects, malformed messages, resource exhaustion and native
exceptions are contained without partial publication.

Source and incremental plug-ins, mapped libraries, standalone object/design
artifacts, relocation, caches and checkpoints carry the exact upstream source,
compiler, standard-library, bridge and ABI identities. Transient native
objects are reconstructed and rebound by stable hierarchy identity; they are
never serialized as pointers.

The protocol and island ownership are worker-ready, but v2 does not include an
automatic partitioner, a kernel-per-worker launcher, or a conservative
parallel scheduler. Those remain post-v2 work and require no replacement of
the current SystemC/TLM ABI.

## Closure evidence

`fsim.systemc.accellera_closure` uses CTest fixtures during ordinary regression
so each underlying witness runs once. Direct invocation of
`cmake/RunAccelleraSystemCClosure.cmake` runs the corpus, backend, integration
and public-contract stages and retains a console log, per-stage logs and a
machine-readable result table.

## Deliberately outside the integration

- binary compatibility with arbitrary external SystemC builds;
- the removed fsim SystemC facade and custom-kernel execution ABI;
- dynamic cross-language hierarchy construction after simulation starts;
- user replacement of either the Accellera or fsim scheduler; and
- automatic adaptation of arbitrary user-defined interfaces at HDL boundaries.
