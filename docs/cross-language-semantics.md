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
connections. SystemC factories may declare typed foreign-child placeholders
whose full paths bind back to VHDL or SV. Thus every VHDL/SV/SystemC
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
8-bit vectors. General ascending/descending ordinal remapping, Boolean/integer
conversion, and preservation of all VHDL nine-state symbols across the
boundary remain v1 work.

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

`std_logic` uses the VHDL standard-logic resolution table. `sv_wire` uses fsim's
4-state wire resolution. Multiple unresolved drivers are an elaboration error.
Strengths are outside v1, so `sv_wire` does not model Verilog strength
resolution.

The vertical slice automatically accepts single-driver aliases and diagnoses a
multiple-driver boundary without a resolver. Because competing runtime driver
values are not yet represented, supplying `std_logic` or `sv_wire` for such a
boundary is also rejected with an explicit “resolution not implemented”
diagnostic rather than being accepted without effect. Multiple executable
process drivers on one signal are likewise rejected. The executable mixed
example has one driver per boundary.

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
The current interpreter and C API implement deposits plus a bounded force mask:
updates continue beneath a forced value and release exposes the latest
underlying value. Integration with the complete multi-driver resolution model
is still planned.
