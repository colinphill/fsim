<!-- SPDX-License-Identifier: Apache-2.0 -->
# Deterministic cross-language semantics

VHDL, Verilog/SystemVerilog, and SystemC define their own scheduling models,
but no standard defines zero-delay ordering between different languages. fsim
therefore defines the following deterministic policy. It is part of fsim's
observable behavior, not a claim about an IEEE or Accellera mixed-language
standard.

The selected VHDL revision and non-standard Synopsys package provenance remain
attached to a VHDL occurrence on either side of a SystemVerilog boundary.
VHDL-87/93/2000/2002 share the deterministic scheduler and conversion rules;
the boundary never upgrades an older unit to VHDL-2008 or re-resolves its
`std_logic_signed`/`std_logic_unsigned` overloads. Interpreter and LLVM O0/O2
preserve exact 137-bit Logic9 planes and ascending/descending bounds in both
SystemVerilog-parent/VHDL-child and VHDL-parent/SystemVerilog-child directions.

This is the **v2 semantic contract**. Schema 2 adds deterministic automatic
target resolution while retaining the v1 scheduler, boundary, and explicit
override behavior.

## Target resolution and explicit overrides

An unqualified instance name is resolved across VHDL,
Verilog/SystemVerilog, and exported SystemC factories in one effective logical
library scope. The parent library is first, followed by first occurrences from
the configured list:

```toml
[elaboration]
search_libraries = ["vendor", "shared"]
```

Repeated `--search-library LIBRARY` options replace that manifest list for one
command while preserving command-line order. Duplicate spellings collapse at
their first occurrence. Ordering makes the queried scope and diagnostics
deterministic; it is not a priority rule. Resolution examines every library in
the complete scope and therefore reports ambiguity if more than one candidate
exists anywhere in it.

VHDL entity matching is case-insensitive; Verilog/SystemVerilog and SystemC
matching is case-sensitive. Resolution must produce exactly one candidate
before any interface compatibility checks. Same-language and parent-library
candidates receive no preference, and interface shape never breaks a tie.

An explicit binding remains an authoritative override:

```toml
[[binding]]
instance = "tb.u_counter"
target = "vhdl:work.counter(rtl)"

[[binding]]
instance = "tb.u_bus"
resolver = "sv_wire"
```

Target forms are:

```text
vhdl:library.entity(architecture)
sv:library.module
systemc:library.factory
```

Schema 2 permits a target, a resolver, or both; an empty binding is invalid.
A resolver-only entry preserves inferred target selection while choosing
cross-language `inout` semantics. A VHDL entity with multiple eligible
architectures is ambiguous unless a configuration or explicit target selects
one. Missing and ambiguous diagnostics include the instance path, requested
spelling, logical-library scope, and canonical candidates.

Configured libraries are queried lazily. Merely declaring an unavailable
library does not fail parsing, checking, or an elaboration that uses only
qualified targets. The first unqualified root or child lookup that needs the
scope reports every unavailable configured library. Explicit qualified tops
and full-path binding targets remain authoritative and do not query the list.

SystemC factories participate under their public `SC_FSIM_EXPORT` or
`SC_FSIM_EXPORT_AS` names. HDL source owns HDL child hierarchy and may select
an official SystemC factory at an explicit binding path; SystemC source exposes
no reverse HDL-proxy facade.

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

The table is the value mapping vendors document (for example AMD UG900,
"Mixed Language Simulation"), and strength is otherwise ignored across the
boundary. fsim adds one rule for a SystemVerilog net on a VHDL `std_logic`
`inout` port: the VHDL driver's `L`, `H` and `W` enter the net's resolution as
weak 0, weak 1 and weak X, and every other value as a strong one, so a VHDL
pull-up (`sda <= 'H'`) against a SystemVerilog `0` resolves to `0`, as it does
in `std_logic` resolution and as it would if VHDL declared the net. Code on
either side reads the resolved net through the table: VHDL sees `'0'`, `'1'`,
`'X'` or `'Z'`, never `'H'`. This is an fsim extension, not a vendor rule.
The v4 kernel resolves such a net itself, in compiled code, when SystemVerilog
and VHDL both drive it (engine-v4-semantics.md S4); the result is the same as
the reference scheduler's.

The bus resolves the same whichever side declares it, but what VHDL reads
back does not. With a pull-up and no other driver, VHDL reads `'1'` from a
SystemVerilog-declared net and `'H'` from its own `std_logic` signal (as
`std_logic` resolution gives). VHDL code that tests the bus should therefore
compare `to_x01(sda) = '1'` (or use `rising_edge`/`falling_edge`, which
accept `'H'`/`'L'`) instead of `sda = '1'`, so that it works on either kind of
bus.

A VHDL port that drives a SystemVerilog net (an `out`, `inout` or `buffer`
formal) is a source of it from elaboration (IEEE 1076-2019 14.7.3.2). Its
driver starts at the port's default, so the default `'U'` makes the net `X`
until VHDL assigns it, and a SystemVerilog `z` on the same net does not hide
it. The net's own drivers start at `z`.

## Drivers and resolution

A boundary net with one driver binds automatically. An `inout` whose actual
resolves in its own language (a SystemVerilog `wire`/`tri`, a VHDL
`std_logic` signal) shares that signal, and each side reads it through the
logic mapping. Any other inout or multi-driver boundary must state one of
(`FSIM-ELAB-BIND-030` otherwise):

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
Multiple unresolved drivers remain an elaboration error. Strengths and charge
storage remain later work; native wired-AND/OR nets are implemented. Exact
nine-state processes use the append-only four-plane Logic9 generated-code ABI
when their operations are in the compiled subset and otherwise fall back per
process. Batch 151 also removes the former 64-bit admission ceiling from
SystemVerilog/VHDL packed-vector width and signedness adapters: focused
129-to-137-bit and 137-to-129-bit conversions retain every mapped bit, while
lossy state-domain conversions still reject explicitly.

## Time and phase lattice

At each timestamp fsim performs, for each delta:

1. ingest all due timed events;
2. run the SystemVerilog regions of the delta that have work (Active,
   Inactive `#0`, NBA, Observed, Reactive, Re-Inactive, Re-NBA; IEEE
   1800-2023 4.4, 4.5) until none of them has work left, together with the
   VHDL processes their changes wake (below);
3. run the delta's generic (VHDL and SystemC) processes resumed for it, in
   stable process-ID order; they read the signal values of the start of
   the simulation cycle (IEEE 1076-2019 14.7.5.3). A SystemVerilog process
   the same events resume is queued in its region by this pass and runs in
   step 4, after the pass;
4. run the SystemVerilog regions again until none has work left, as in
   step 2;
5. only then run the delta's update phase, which commits VHDL signal
   transactions and SystemC channel updates (IEEE 1076-2019 14.7.5.3);
6. resolve changed signals/nets and enqueue their affected processes for the
   next delta;
7. run VHDL postponed work, `$strobe`/`$monitor`, trace callbacks, and debugger
   signal breakpoints; and
8. repeat delta cycles until quiescent, then advance time.

The reference scheduler implements this directly: before each generic phase
of a delta it starts any SystemVerilog region that has work
(`Scheduler::run`, `begin_systemverilog_batch`), so each generic phase
starts only while the SystemVerilog regions are empty, and the generic
Active phase runs as one batch, ahead of the SystemVerilog work the batch
queues. Both engines follow this one rule (gate BA). The compiled
kernel runs a VHDL round inside the generic Active phase at once when its
activation is a task of that phase, and queues the round's update phase
behind pending SystemVerilog work into the delta's generic Inactive phase;
a round of an activation outside a generic queue waits for that work
whole (engine-v4-semantics.md S16).

An immediate SystemC notification re-enters the active worklist at the same
timestamp. It never runs a callback recursively. A process awakened by the
update/resolve phase runs in the next delta, not later in the completed active
phase.

A change made by SystemVerilog work (a blocking or continuous assignment in
the Active region, or an NBA update) wakes VHDL processes in the same delta,
in the next round of the Active region, with the SystemVerilog processes it
wakes and before that region's next NBA updates. The change is an event of
that delta, so `S'EVENT` and `rising_edge` see it. A VHDL process's signal
assignments still wait for the update phase. A SystemVerilog flop and a VHDL
flop on one SystemVerilog clock therefore both sample the values from before
the edge. This holds for a VHDL process that reads the SystemVerilog object
itself (a VHDL `in` port whose actual is a SystemVerilog variable or net):
it sees each value the SystemVerilog regions give it, including one replaced
later in the same delta.

A SystemVerilog value that reaches a VHDL *signal* through a port (a
SystemVerilog instance's output connected to a VHDL signal, with or without
a width or type conversion) is a transaction on that signal's driver. Each
SystemVerilog change of the delta gives the driver a new zero-delay
transaction, which replaces the previous one (IEEE 1076-2019 10.5.2.2), and
the signal takes the driver's value in the update phase of step 5, after all
of the delta's SystemVerilog regions. So the VHDL signal, and every VHDL
process sensitive to it, sees only the value the delta's SystemVerilog work
leaves: a leaf that writes `00`, then `01` after `#0`, then `10` and `11` as
nonblocking updates gives the VHDL signal `11` in that delta and never `01`;
if the delta ends at the value the signal already has, the signal has no
event. Cost: none in the reference scheduler; in the compiled kernel the
VHDL round, or its update phase, runs as one host task of the delta either
way, only in a later queue of it (engine-v4-semantics.md S16). Witness:
`tests/fixtures/systemverilog/scheduling/mixed_region_port.sv` on both
engines (`fsim.application.systemverilog-scheduling-witnesses` and its
`.v4` twin).

The same holds for an unresolved VHDL signal that a SystemVerilog port drives
directly (`std_ulogic_vector` actual of a SystemVerilog `output reg`): a
SystemVerilog flop and a VHDL flop on one VHDL clock both sample the values
from before the edge. The VHDL flop that reads the SystemVerilog flop's
output runs in step 3 and reads the value from before the SystemVerilog
flop's nonblocking update in step 4, and the SystemVerilog flop that reads
a VHDL flop's output reads it before that flop's update phase (step 5):
each second stage runs one cycle behind its first. Witness:
`tests/fixtures/systemverilog/scheduling/mixed_clocked_port.sv` (VHDL top
`mixed_clocked_port.vhd`), same tests.

No standard orders zero-delay events across languages, and vendors document
none. Beyond the rules above, the order in which the two languages' processes
run within one time step, and the number of `#0` or delta steps after which a
change crosses the boundary, are unspecified and may change between fsim
releases. The contract is the values at time-step boundaries, which the LRM
rules and the logic mapping determine; tests compare those, not the
interleaving within a step.

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
