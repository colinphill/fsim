<!-- SPDX-License-Identifier: Apache-2.0 -->
# Deterministic cross-language semantics

VHDL, Verilog/SystemVerilog, and SystemC define their own scheduling models,
but no standard defines zero-delay ordering between different languages. fsim
therefore defines the following deterministic policy. It is part of fsim's
observable behavior, not a claim about an IEEE or Accellera mixed-language
standard.

This is the **v1 semantic contract**. The current vertical slice implements the
four scheduler phases and stable ordering. It also executes a bounded mixed
hierarchy made from simple VHDL/SV instances and whole-signal connections. It
does not yet implement every boundary conversion, driver-resolution rule, or
language-specific queue described below.

## Explicit binding

fsim never guesses a cross-language target by name. Each cross-language
instance/component must have one manifest entry:

```toml
[[binding]]
instance = "tb.u_counter"
target = "vhdl:work.counter(rtl)"

[[binding]]
instance = "tb.u_bus"
target = "sv:work.bus_target"
resolver = "sv_wire"
```

Target forms are:

```text
vhdl:library.entity(architecture)
sv:library.module
systemc:plugin.factory
```

The schema-1 loader validates these prefixes. The current elaborator resolves
VHDL and SV targets and constructs typed `systemc:` factories, recursively
building a common hierarchy with named or positional whole-signal port
connections. SystemC factories may construct `SC_FSIM_HDL_MODULE` proxies,
bind their ordinary ports, and select the HDL implementation with an explicit
full-path manifest binding. Thus every VHDL/SV/SystemC
parent→child language direction is explicit and supported by the same
elaborator. A binding whose instance path is absent is an error;
cross-language targets are never inferred from the source unit name.
Generic/parameter specialization remains forthcoming.

## Boundary types

The portable boundary type set is deliberately small:

- scalar 2-, 4-, or 9-state logic;
- Boolean values;
- width- and signedness-compatible integers; and
- packed vectors.

Records, VHDL arrays other than packed vectors, SV unpacked aggregates,
structs, interfaces, and arbitrary SystemC C++ types require a same-language
wrapper. A wrapper exposes only portable boundary ports.

Vector elements map by ordinal position: the leftmost source element maps to
the leftmost destination element, independent of whether either declaration
uses an ascending or descending index range. Width and signedness mismatches
are errors unless an explicit same-language conversion appears in the design.

The current executable subset aliases an equal-width whole parent signal to
each connected child port. It validates width, signedness, and implicit loss
into a 2-state destination. Its demonstrated boundary uses matching descending
8-bit vectors. The shared signal retains its owning language's value domain;
each VHDL or SV process read/write view applies the table below, with automated
runtime evidence in both hierarchy directions. General ascending/descending
ordinal remapping and Boolean/integer conversion remain v1 work.

Batch 101 expression profiles and runtime-base packed read selections are
same-language SystemVerilog semantics. Their width, signedness, state-domain,
and declared-direction metadata remain inside the owning process and native
cache key; they do not relax the equal-width whole-signal mixed-language
boundary above. Streaming concatenations likewise produce an ordinary packed
value before any later boundary use. Dynamic selected port actuals and any new
cross-language resize or state-domain coercion remain deferred.

### Logic mapping

| Source | Destination | Result |
|---|---|---|
| VHDL `0`, `L` | SV/SystemC 4-state | `0` |
| VHDL `1`, `H` | SV/SystemC 4-state | `1` |
| VHDL `Z` | SV/SystemC 4-state | `Z` |
| VHDL `U`, `X`, `W`, `-` | SV/SystemC 4-state | `X` |
| SV/SystemC `0`, `1`, `X`, `Z` | VHDL logic | same logical symbol |

Conversion into a 2-state destination is rejected if the source may contain an
unknown or high-impedance value. fsim does not silently coerce `X` or `Z` to
zero.

## Drivers and resolution

A boundary net with one driver binds automatically. An inout or multi-driver
boundary must state one of:

```toml
resolver = "std_logic"
resolver = "sv_wire"
```

The common executable value path retains all nine VHDL states for a Logic9
signal and `0`, `1`, `X`, and `Z` for a Logic4 signal. `std_logic` uses the
complete standard nine-state table, including the single-driver `-` rule;
`sv_wire` uses four-state wire resolution. Each executable process owns an
independent domain-preserving driver slot, including for whole/slice blocking,
NBA, future, inertial, and projected writes. Native SV `wire`/`tri` and VHDL
`std_logic`/`std_logic_vector` signals select their policy automatically;
explicit mixed-boundary resolvers select it on an otherwise unresolved parent.
Multiple unresolved drivers remain an elaboration error. Strengths,
wired-AND/OR nets, and charge storage remain v1 work. Exact nine-state
processes currently use the reference evaluator in compiled mode until the
Logic9 generated-code ABI path is added.

## Time and phase lattice

At each timestamp fsim performs:

1. ingest all due timed events;
2. run active VHDL, SV, and SystemC processes in stable process-ID order;
3. run SV `#0` inactive events;
4. commit VHDL signal transactions, SystemC channel updates, and SV NBA writes
   in one update phase;
5. resolve changed signals/nets and enqueue their affected processes for the
   next delta;
6. run VHDL postponed work, `$strobe`/`$monitor`, trace callbacks, and debugger
   signal breakpoints; and
7. repeat delta cycles until quiescent, then advance time.

An immediate SystemC notification re-enters the active worklist at the same
timestamp. It never runs a callback recursively. A process awakened by the
update/resolve phase runs in the next delta, not later in the completed active
phase.

The mixed vertical-slice test exercises this rule: an SV testbench clock wakes
a VHDL `rising_edge` process, the VHDL update commits `q`, and an SV continuous
assignment observes the aliased value in a following delta. A second automated
test elaborates the reverse hierarchy direction: a VHDL top commits a packed
value and a bound SV child observes and inverts it in the following delta.

Stable process IDs derive from deterministic elaboration order, not pointer
values, thread timing, or hash-table iteration. Random facilities default to
project seed `1`; each process receives a stream derived from its stable ID.
Numeric manifest, CLI, and native-session seeds reproduce the same per-process
streams. `--seed=random` is an explicit opt-in to host-entropy seeding; fsim
prints the selected numeric seed so the run can be reproduced.

Simulation aborts when delta count exceeds `max_deltas` (default `100000`) at
one timestamp. The diagnostic includes active process orders and recently
changed signals to help locate an oscillation.

## Visibility

Only committed value changes are externally visible. VCD records initial
values and committed changes, never transient internal register values or an
uncommitted NBA. Signal-change breakpoints and synchronous C callbacks observe
the same commit boundary.

A debugger deposit changes the current stored value and activates dependents.
A force overrides effective driver resolution until release. Release restores
the value determined by drivers; it does not restore a historical snapshot.
The interpreter and C API implement deposits plus a force mask: each driver
continues updating beneath a force and release exposes the latest resolved
underlying value. Native C signal metadata identifies resolved objects and
driver objects expose their pre-resolution process-owned values.
