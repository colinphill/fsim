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
toolchains. fsim compiles selected model sources against its selected
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

A VHDL, Verilog, or SystemVerilog instance may resolve a SystemC factory from
the compiled library catalog. An elaboration top can use `LIBRARY.FACTORY`;
add the `systemc:` prefix when a name is ambiguous across languages. Ports use
ordinary official
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

### C++ kernel-backend migration

Internal C++ backend callers now submit a `SystemCKernelDirectRequest` to
`SystemCKernelBackend::request()` and inspect a `SystemCKernelDirectResult`.
The request is a variant of operation-specific, owning payloads; it carries
the semantic island and sequence identities directly. A caller selects the
corresponding receipt alternative for lifecycle or execution results and
closes the backend explicitly. The former serialized message exchange,
handshake, framing, and loopback replay are not part of this interface.

`kernel_backend_direct.hpp` declares this C++ interface. The native plug-in
C ABI and its version are unchanged: model authors continue using the
`SC_FSIM_EXPORT` helpers and do not need to adopt the internal request types.
Value, TLM, observation, and transaction-record serializers remain available
where persisted artifact formats require them; they are not used to marshal
an in-process kernel call.

For installed C++ SCV probe callers, `ScvResourceMetrics::serialized_bytes`
is now `record_payload_bytes`. The metric counts standalone persisted
`TransactionRecord` payload bytes, not a removed transport envelope; update
source references to the new member name.

## Workspace compilation and linking

The current directory is the workspace. fsim stores a local library named
`models` in `.fsim/libraries/models` and manages its object names, plugin image,
and factory catalog. Compile the translation units, then finalize the library:

```sh
fsim systemc compile --library models -I include -D MODEL_REV=2 \
  --compile-option=-O2 bridge.cpp helper.cpp
fsim systemc link --library models
fsim elaborate --top models.bridge --snapshot demo
fsim simulate --snapshot demo
```

Omitting `--library` selects `work`. A compilation command can contain several
source files; each produces one managed native object. Helper translation
units can be compiled before their exported modules. `systemc link` consumes
the library's current objects and publishes the complete sorted factory and
construction-parameter catalog only after a successful link and ABI check.
Elaboration locates these factories by name without plugin filenames.

Recompiling a source replaces that source's object and invalidates the linked
factory catalog until `systemc link` runs again. A failed compilation keeps
the previous object catalog, and a failed link keeps the previous linked
catalog if the objects have not changed. `fsim library objects models` lists
managed object IDs; `fsim library delete-object models OBJECT_ID` removes an
object and invalidates the plugin catalog. Snapshots embed the selected native
images and remain usable after their producing sources or library objects
change. Re-elaboration replaces the selected snapshot. Without `--snapshot`,
elaboration and simulation use `default`.

Use `fsim library map models /path/to/library` to map an external managed
library. fsim records the mapping in `.fsim/libraries.toml`. See
[workspace mode](workspace-mode.md) for library and snapshot lifecycle rules.

`--compiler`, `--include`, `--define`, and `--compile-option` control source
compilation. `--compiler`, `--link-option`, and `--link-library` control linking.
The compiler identity used for linking must match the objects. Both phases
accept `--verbosity quiet|normal|verbose`; verbose output includes source,
generated artifact, and selected option details.

The driver invokes GCC, Clang, MSVC, or clang-cl directly with an argument
vector. Its fingerprint includes ordered source content, dependency closure,
options, linked inputs, compiler identity and environment, target and shared
library format, C++ mode, CRT, fsim ABI, and the selected Accellera runtime.
GCC-like dependency files and MSVC `/sourceDependencies` JSON extend the cache
identity to transitive headers and supported compiled-header inputs. Inputs
that cannot be modeled safely, including volatile predefined macros, are
rejected by incremental compilation.

The low-level cached compiler APIs additionally publish a versioned key, size,
and SHA-256 record under a per-key process lock. The shared library is installed
before metadata becomes the commit point. Missing, corrupt, stale, incomplete,
or incompatible pairs are cache misses, and the locked writer repairs abandoned
staging state. Workspace compilation publishes through its library transaction.

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

ABI version 4 uses fixed-width integers, C pointers, callbacks, and explicit
byte views. The registrar publishes typed factory schemas. During construction,
the official bridge registers root modules and boundary ports and returns
stable opaque handles. At runtime, the host supplies boundary reads and writes,
the current femtosecond time, and the wait-for-input-or-native-activity hook.
There are no host callbacks for custom processes, events, waits,
notifications, primitive-channel updates, or legacy foreign children.

Registration is buffered and validated before factories or image ownership
become visible. Missing symbols, ABI mismatch, malformed schemas, construction
failure, and escaped callbacks reject the transaction without partial
publication. Native exceptions are contained at the boundary. Accellera roots
remain alive for the owning simulation and are torn down after the native
kernel finishes. Loaded images remain mapped through process teardown because
Accellera registries can retain native type information. Managed replacement
uses fresh physical artifact names and changes the catalog reference, which
also avoids overwriting a loaded DLL on Windows.

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

The in-process backend uses typed owning requests and results. Stable island,
hierarchy, object, endpoint, transaction, and sequence identities survive
snapshot publication, together with exact femtosecond time, delta, and region.
Malformed state, resource exhaustion, and native exceptions are contained
without partial publication.

Source and incremental plug-ins, mapped libraries, standalone object/design
artifacts, relocation, caches and checkpoints carry the exact upstream source,
compiler, standard-library, bridge and ABI identities. Transient native
objects are reconstructed and rebound by stable hierarchy identity; they are
never serialized as pointers.

The low-level C++ incremental API still exposes immutable `.fsimscobj` and
`.fsimscplugin` directory publication for SDK callers. Those APIs require a
fresh output directory; the workspace layer owns replacement and metadata
transactions for CLI users. See the [ABI and schema reference](abi-schema-reference.md)
for the persisted format identities.

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
