<!-- SPDX-License-Identifier: Apache-2.0 -->
# Standard Delay Format support

Fsim accepts SDF 4.0 directly and SDF 2.1 and 3.0 through explicit revision
adapters. The source-tree SDF layers preserve exact normalization,
hierarchy resolution and portable source identity, apply that immutable
representation to Verilog-1995/2001/2001-noconfig/2005 and
SystemVerilog-2005/2009/2012/2017 timing, and annotate the timing generics of
VITAL cells at elaboration (IEEE 1076.4-2000 clause 5) without widening the
selected SDF or HDL revision.

## Supported input and value policy

The clean-room frontend preserves ordered header values, exact signed decimal
and partial `min:typ:max` values, timescales, conditions, delay families, timing
checks, timing-environment constructs and empty, exact or wildcard instance
selectors. Revision adapters reject constructs, spellings, arities and ordering
that do not belong to the selected profile.

An application selects `min`, `typ` or `max`, converts the exact rational value
through the SDF and design timescales and rounds once at the simulator-precision
boundary. Missing selected slots, negative effective delays, arithmetic
expansion and tick overflow fail transactionally. No floating-point or host-
locale conversion participates in annotation identity.

## Verilog and SystemVerilog timing application

Before publication, each resolved mapping becomes an immutable target plan
keyed by stable elaborated timing-object identities. Validation covers cell and
object kind, delay/check arity, duplicate ownership, widths and source spans.
The supported application surface is:

- absolute and incremental `IOPATH`, including parallel/full paths, polarity,
  edges, conditions, `CONDELSE` and state dependence;
- `INTERCONNECT`, `PORT`, `NETDELAY` (OVI SDF 1.0, 2.0, 2.1 and IEEE SDF 4.0),
  `MIPD` and `DEVICE` delays for nets, ports,
  primitives, UDPs, gates, switches and continuous assignments;
- governed one-, two-, three-, six- and twelve-value transition lists;
- `$setup`, `$hold`, `$setuphold`, `$recovery`, `$removal`, `$recrem`, `$skew`,
  `$timeskew`, `$fullskew`, `$width`, `$period` and `$nochange` limits;
- edge and conditional timing-check expressions, notifiers, negative checks,
  `PATHPULSE`, percentage pulse controls and `RETAIN`; and
- deterministic source/SDF precedence, multiple-file ordering and rollback-
  safe reannotation.

Interpreter and LLVM execution use the same precomputed effective timing
records. Inertial and transport cancellation, same-time scheduler regions,
strength/resolution state, switches, continuous drivers and force/release
interactions therefore do not perform hierarchy lookup or value conversion on
the event hot path. Disabling SDF preserves the original no-annotation timing
identity and does not allocate observation records.

## VHDL/VITAL and mixed-language timing application

Two things are called VITAL SDF support in the source tree, and only one of
them reads SDF files.

- The `sdf_vital_*` headers (target plan, models, path timing, precedence,
  phases, archive, observability) describe VITAL targets, paths, checks and
  their effective timing as immutable records with unit tests. They are a
  library-level planning layer: nothing in the session file-input boundary
  calls them, so they do not annotate a design from an SDF file.
- **VITAL model annotation** (IEEE 1076.4-2000 clauses 5.1 and 5.2) is what
  `fsim elaborate --sdf` does with the VHDL cells an SDF file names. It is
  described in the next section.

VHPI and VPI enumerate stable read-only timing objects and bounded callbacks
for the Verilog and SystemVerilog records above. Debugger, callbacks, internal
trace and VCD share deterministic effective, pending-transaction and violation
events. Disabled observation returns before object lookup and cannot perturb
scheduling. VITAL timing generics are not published as timing objects.

## VITAL models: backannotation of timing generics

A VITAL model takes its timing only through its backannotation timing
generics (IEEE 1076.4-2000 clause 5). `fsim elaborate --sdf FILE` reads the
SDF file after the first elaboration of the design, finds the VHDL cells it
names, builds the generic names and values of the VITAL SDF map (5.2.7) and
elaborates the design again with those generics, so the model is lowered
with the annotated values. This is the direct SDF import of 5.1.1: the state
of the model at the start of simulation is the one a configuration
declaration with the same generic values would give, and the interpreter and
the compiled v4 kernel run the same lowered design. A design without an SDF
file is elaborated once, as before.

Because a generic is a constant of elaboration, the timing generics of a VITAL
cell are annotated only at `fsim elaborate`. `fsim simulate --sdf`, the Tcl
`fsim::sdf` commands and the C and C++ sessions reject an SDF file that names
VHDL cells (`FSIM-SDF-VITAL-008`) instead of reannotating at a safe point:
a live change of a generic cannot be exact. Verilog and SystemVerilog cells of
the same file are still annotated there. An SDF file with VHDL cells only
leaves nothing for the Verilog session and is not published with the design.

The VHDL cell of a mixed SDF file is found by the same instance resolution as
every other cell, so the path elements name VHDL block, generate and
component instance labels (`g[1].u`, `c.v`; 5.2.4). A bit spec `g[1]` names
one expansion of a FOR generate statement and `g[0:3]` the expansions between
the two indices in either direction. A label that does not exist, or an index
outside the generate range, names no instance (`FSIM-SDF-RESOLVE-002`).

Supported constructs and their generics (5.2.7), with `A`, `B` the port names
and `<cond>`, `<edge>` the suffixes of 5.2.7.2.1 and 5.2.7.3:

| SDF | Generic |
| --- | --- |
| `IOPATH A B`, `COND ... IOPATH`, `IOPATH (edge A) B` | `tpd_A_B[_<cond>][_<edge>]` |
| `PORT A` | `tipd_A` of the cell's instance |
| `INTERCONNECT S I.A` | `tipd_A` of the instance `I` below the cell's instance |
| `DEVICE [Z]` | `tdevice_<InstanceName>[_Z]` |
| `SETUP`, `HOLD`, `RECOVERY`, `REMOVAL`, `SKEW` | `tsetup_`, `thold_`, `trecovery_`, `tremoval_`, `tskew_` `A_B_<full suffix>` |
| `SETUPHOLD`, `RECREM`, `NOCHANGE` | the two generics `tsetup_`/`thold_`, `trecovery_`/`tremoval_`, `tncsetup_`/`tnchold_` |
| `BIDIRECTSKEW A B` | `tskew_A_B_...` and `tskew_B_A_...` |
| `WIDTH`, `PERIOD` | `tpw_A[_<simple suffix>]`, `tperiod_A[_<simple suffix>]` |

`ABSOLUTE` replaces and `INCREMENT` adds to the generic's present value (its
default, its value from a generic map, or an earlier annotation), in the
order of the SDF file and across files in command-line order. The delay list
fills the generic by its type (5.2.6, Tables 1 and 7): one value gives every
transition; two, three or six values follow Table 1; twelve values fill all;
lists of four or five values and of seven to eleven are read with trailing
empty values; an empty value `()` and a missing component of a min:typ:max
triple leave the transition as it was; and the transitions through `X` of a
`VitalDelayType01ZX` that a list of at most six values fills are derived from
the six explicit ones as the selection of a `VitalDelayType01Z` delay does
(Table 7). A list longer than the generic holds is an error
(`FSIM-SDF-VITAL-004`). If the SDF data gives no value for a generic, the
elaboration value stays.

Errors (5.2): an SDF construct whose generic name is not a legal VHDL
identifier (`FSIM-SDF-VITAL-001`), and a generic the entity does not declare
(`FSIM-SDF-VITAL-002`). Both apply to every VHDL cell an SDF file names:
the strict reading of the NOTE in 5.1.2, that an annotator is neither
required nor prohibited from annotating a model that is not VITAL compliant,
leaves the choice to the annotator, and fsim does not look for the
`VITAL_Level0` attribute, so a cell without it is annotated by generic name
like any other. The other VITAL codes are in the
[diagnostic catalog](diagnostics.md).

Unsupported constructs have no effect and are not errors (5.2, 5.2.7.1):
`NETDELAY`, `PATHPULSE`, `GLOBALPATHPULSE`, `CORRELATION`, `CONDELSE`,
`MIPD`, `PATHCONSTRAINT`, `SUM`, `DIFF`, `SKEWCONSTRAINT` and timing checks
with `SCOND` or `CCOND`.

Vector delay generics (`VitalDelayArrayType*`) take bus ports with an index or
range: the row-dominant mapping of 5.2.7.3.3.1 (one indexed port) and
5.2.7.3.3.2 (two indexed ports, paired when the generic is as large as the
first port) computes the element of each index of each range from the VHDL
port bounds and the generic's range. An index with no vector generic is
`FSIM-SDF-VITAL-007`, and a failed mapping `FSIM-SDF-VITAL-012`. `RETAIN`
values map by Table 2 of 5.2.6 (`IOPATH` only); more than three retain values,
more than six delays, or three retain values or tristate delays on a generic
that is not `VitalDelayType01ZX` are `FSIM-SDF-VITAL-011`. A timing-check
`COND` may wrap an edge, `(COND c (posedge clk))`.

Multiple source mode (5.2.7.1.5, 6.4.1). `INTERCONNECT` entries from two
sources to one `tipd_` generic enable the mode: each source of the input's
actual gets a delay location, initialised to the generic's value at the time
the second source appears (rule b); an `INTERCONNECT` updates its source's
location and a `PORT` entry all of them (rules c and d). Drivers of the actual
that no entry names are one more source, with the generic's value. The
elaborated design gives each source a shadow signal: every driver of the
source keeps writing the net and also writes the shadow (a dual write, nothing
is rerouted; a `VitalDelay` driver mirrors its assignment), and the wire delay
call selects, on every event on its input, the sources whose shadow had an
event in the delta cycle (all sources at initialization and for a change no
source made, such as a deposit), applies `VitalCalcDelay` of each location
with the new and old value, and assigns the minimum with transport delay. A
driver the dual write cannot cover or a source that drives nothing is
`FSIM-SDF-VITAL-013`. Both engines run the same operation.

A vector interconnect delay generic (`VitalDelayArrayType*`, whose bus port is
delayed by a wire delay generate, `VitalWireDelay(A_ipd(i), A(i), tipd_A(i))`)
has the mode element by element. An `INTERCONNECT` entry names the elements of
its destination by the row-dominant index mapping of 5.2.7.3.3.1 (`A[3]`, a
range, or `A[0:3]` for the whole port); rule a is applied to each element, so
an element that a second source reaches gets one location for each source,
initialised with that element's value (rule b), and an element with one source
keeps the plain behaviour. A `PORT` entry updates every location of the
elements it names. The shadow signals are buses like the net (one for each
source instance and one for the drivers no entry names); the call of an
element selects the sources whose shadow had an event in the delta cycle in
that element, takes the location of each from that element's row, and a source
that no entry names for the element has the element's value. The call reads
and assigns one element of the bus (`VitalDelay::input_offset` and
`output_offset`) and waits only on a change of that element. Two ports of one
instance as sources of one element and a call that cannot be found for an
element are `FSIM-SDF-VITAL-013`.

An `INTERCONNECT` or `PORT` in the cell of a Verilog or SystemVerilog parent
whose destination is an input port of a VHDL instance below it annotates that
instance's `tipd_` generic by 5.2.7.1.3 and 5.2.7.1.4; the SystemVerilog
session does not delay the net a second time, and a `simulate --sdf` file with
such an entry is `FSIM-SDF-VITAL-008` like any VHDL cell. A VITAL entity may be
instantiated directly from SystemVerilog: its standard-logic ports are checked
by their VHDL type, not by the four-state storage the SystemVerilog net gives
them.

Not covered, and why:

- The annotation replaces the generic of the entity of the instance, so a
  component generic that a binding maps with an expression of its own is
  replaced by the SDF value.
- `tdevice_<InstanceName>` follows three sentences. 5.2.7.1.6: "The
  <InstanceName> is derived from the name of the SDF instance to which the
  DEVICE entry applies (it is not derived from the port_instance construct).
  If the SDF instance has a hierarchical name, the lowest level instance name
  is the <InstanceName>." 5.2.4: the SDF instance is a path whose last element
  "shall denote a component instance", and the timing data of the cell "is
  mapped to the backannotation timing generic(s) of the specified instance(s)"
  (5.2.3). 4.3.2.1.3.12: a device delay generic is "the delay associated with a
  device (primitive instance) within the cell model". So the generic is a
  generic of the entity of the annotated component instance, named after that
  instance's own label (`DEVICE Z` on `Top.I1.P1` sets `tdevice_P1_Z` of `P1`),
  as the example of 5.2.7.1.6 shows with `tdevice_P1_Z`. The NOTE that the name
  "will be the label for a VITAL primitive concurrent procedure call" describes
  the model, not the lookup; fsim does not look inside the architecture.

## Session file-input boundary

The CLI, Tcl and C session adapters connect file input to the existing parser,
resolver, planner and timing application. Their connected target kinds are
Verilog/SystemVerilog specify paths (`IOPATH`) and timing checks; the VHDL
cells of an SDF file are annotated at `fsim elaborate` (VITAL models, above). An
`INTERCONNECT`, `PORT` or `NETDELAY` delay is recorded in the elaborated design
(`sdf_net_delays`) with its terminals and effective transition delays, and VPI
publishes it as an intermodule path, delay device and delay terms (IEEE
1800-2023 37.37, 37.45). The delay also delays the net in simulation (IEEE
1364-2005 16, 1800-2023 38.10): a port connection shares one signal between
the driver side and the instance behind the port, so annotation splits it for
each annotated load port only. The load gets a signal of its own, a copy of
the net that a new process writes with the delay's twelve transition delays
and their pulse limits (`WriteNetDelay`, below), and the processes of the load's
instance (those whose names lie below its path) read the copy. The driver
side, and every other load of the net, keeps the undelayed value: a reference
to the source yields the undelayed value and one to the load the delayed one.
A `NETDELAY` is a `PORT` delay on every load it reaches (IEEE 1364-2005
16.2.4): on an input or inout port it is that port's interconnect delay from
all its sources; on a net it is the delay of every input and inout port of a
Verilog or SystemVerilog instance that shares the net's signal (the session
reads the instances' port declarations and directions from the design IR and
the SystemVerilog HIR, and takes the ports whose path names the net's signal),
one record per load whose identity names the load, so that a later record of
the same load replaces it. `NETDELAY` on an output port is an error
(`FSIM-SDF-INTERCONNECT-002`; "NETDELAY delays can only be annotated to input
or inout module ports or to nets"), and one on a net that reaches no such port
warns with `FSIM-SDF-SESSION-004`. A port with a width adapter of its own is
not on the net's signal and is not found. NETDELAY is unsupported for VHDL/VITAL
targets (IEEE 1076.4-2000 5.2.7.1), and an unsupported construct modifies no
back-annotation generic and has no other effect (5.2): a NETDELAY whose loads
include VHDL entity ports applies to the Verilog and SystemVerilog loads only,
the VHDL loads get no delay and no error, and a net with only VHDL loads warns
with `FSIM-SDF-SESSION-004` citing 5.2.7.1 (`PORT` and `INTERCONNECT` into
VHDL, the `tipd_` generics, are separate). SDF 3.0 has no `NETDELAY`
(`FSIM-SDF-30-001`). VPI publishes the records as `PORT` delays
(`vpiMIPDelay`; IEEE 1800-2023 Table 32-3 gives `PORT`, `NETDELAY` and
`INTERCONNECT` the same interconnect delay structure).
The records that end at a load port apply in order of occurrence (IEEE
1364-2005 16.3): a `PORT` delay after an `INTERCONNECT` replaces the delay of
every source, an `INTERCONNECT` after a `PORT` changes only the delay from its
own source (16.2.4), and a record that repeats an earlier one applies at its
later place; an `INCREMENT` record adds to the delay (and the pulse limits) an
earlier file annotated to the same load. When every driver of the net reaches the load with the same
delay and drive strength, and the load does not drive the port itself, one copy
of the net is delayed as a whole (driven with the strength of the drivers).
Otherwise each source has a delay of its own (16.2.4): the drivers of the net
are grouped by the source port they stand for (the processes of the port's
instance that write the net, nearest instance first), by drive strength within
a source, and the drivers that no `INTERCONNECT` names form one more group with
the `PORT` delay. Each driver keeps writing the net and also writes a copy of
its group's contribution (a dual write, so the net itself, its other readers,
forces and the VPI drivers stay exact), and the load's copy is driven from each
group's copy with the group's delays and its own drive strength, resolving the
groups as the net resolves its drivers (IEEE 1800-2023 28.12; the copy keeps the
net's implicit driver and charge). The drivers of one source that differ in
strength are separate groups of equal delay, so that the strength of every
driver reaches the load as it is (`net_strength`); the load resolves the delayed
strengths, which equals delaying the resolution of the source's drivers when
their changes do not overlap within the delay (a pulse is filtered by the limits
of each strength's copy, not of the resolved value). A copy that has a driver
with a drive strength is a host signal, and so are its drivers (the static
kernel keeps a member whose interconnect delay targets a signal it does not own
on the host). A driver that also
reads, waits on or is sensitive to the net is a driver like any other: it
contributes exactly what it writes, whatever it computed that from, and keeps
reading the real net (`net_writer_reads`).

A driver inside the load instance (an inout port the instance also drives, a
nested instance below the load) is the load's own: its contribution reaches the
net at once, and the load's own reads, through a group of its own with no delay.
The delay is the one from another port to the load, and a driver inside the load
is not routed through the interconnect into itself (16.2.4; `net_load_drives`).
The load's copy then resolves the delayed groups with the load's own undelayed
group, and the processes of the load instance read the copy for every operation
that names the port: reads, waits, sensitivity, `$monitor`, `%v`, the target of a
`force` or `release`, VITAL timing checks; their writes keep the net. A driver
of the load that the dual write cannot copy (an operation outside the
`Write*` family) leaves the load undelayed, as does a net of the port of the
design itself that a process of the design also drives (what drives it from
outside writes the same net).

A port connection that passes bits on (`port_actual_N`, `port_result_N` and the
bidirectional `$inout_N` of an inout port connected to a select or
concatenation: reads, selects, concatenates and writes, no computation) is part
of the net: the drivers of the signals it reads are drivers of the load, so a
source or a load connected to a select of a vector, or an inout port connected to
a concatenation, has its own delay (`net_sources`, scenario B). For a net with
per-source delays each group gets a copy of the connection that reads the group's
copies and writes the group's copy of what it drives. A connection of an
expression (`.a(x & y)`) is an implicit continuous assignment (IEEE 1800-2023
23.3.3.2): a driver outside the load, of the `PORT` delay group.

A source whose drivers are a primitive, a VHDL entity, a nested instance, a
constant or a pull-up is grouped like any process of the instance
(`net_sources`, scenarios A, C to F; a pull-up or weak driver is a group of its
strength). An `INTERCONNECT` whose source port drives nothing on the load's net
(the port is not on the net: the net is assigned from it in the parent, or the
source is not an output of the net's instances) is annotated anyway, as the
delay from all sources, like a `PORT` delay, and in order of occurrence (IEEE
1364-2005 16.2.4: "a warning message is issued, but the delay to the load port
is annotated anyway ... treated as if it were the delay from all source ports"),
with the warning `FSIM-SDF-SESSION-005` (`net_sources`, scenario G).

A switch is not covered. IEEE 1364-2005 16.2.4 (IEEE 1800-2023 32.4.4) defines
an interconnect delay as "the signal propagation delay from an output or inout
module port to an input or inout module port", annotated "only between module
ports, never between primitive pins". The terminals of a `tran`, `rtran`,
`tranif` or other switch of the design are pins, the clause gives no source and
load across a bidirectional switch (nothing says what the delay of a value that
the load pushes back into the net through the switch is), and a copy of the net
for the load cannot conduct to the switch's other side without conducting the
delayed value back to the source side. A switch inside the load instance on the
port, or a switch that drives a net with per-source delays, leaves the load
undelayed with the warning `FSIM-SDF-SESSION-004` that says so (`net_switch`,
scenario 1). A switch on a net that every driver reaches with the same delay
(a `PORT` delay, or one source) is no obstacle: the net is delayed as a whole,
with what the switch carries into it (scenario 2).

A delay on a bit or part select of a port delays
only those bits: the records apply bit by bit, and the copy's driver writes
each run of bits with equal delays on its own (bits without a delay at once).
A delay on a port of the design itself (an input or inout port of the top
module) delays what the design reads from it (IEEE 1364-2005 16.2.4): the
load instance is the top module, so every process of the design below the
root reads the copy, while whatever drives the port from outside (a VPI
deposit or force, a library write) writes the net. A port of the design itself
that a process of the design also drives is not delayed: what drives it from
outside writes the same net, and cannot be told from the design's own driver,
whose contribution would have to reach the copy undelayed.

A foreign write of a net with per-source delays. The group shadows carry only
what the sources write, so a deposit, force or release of the net itself from
outside the design (`vpi_put_value`, VHPI `vhpi_put_value`, `acc_set_value`,
`tf_put*`, the Tcl `fsim::deposit_signal`, `fsim::force_signal` and
`fsim::release_signal`, the C API) would never reach a load. It is a change no
source made, which IEEE 1076.4-2000 6.4.1 treats the same way for the VITAL
multiple source mode, and it reaches each such load's copy after the minimum,
over the load's groups, of the group's delay for the transition of each bit
(from the copy's current bit to the written bit; the transition from the
forced value to the net's value after a release, which on a net the v4 kernel
owns is read once its drivers have run again, in the Inactive region of the
step): bits may arrive at different times. A deposit is a deposit of the copy at that time (the copy keeps the
value until a driver update resolves it again), a force a force of the copy
until the matching release, and a release a release of the copy, which then
returns to the resolved value of its group drivers. A deposit that does not
change the net reaches no copy. A write whose minimum is zero reaches the copy
in the same step. The minimum reads the drivers' current tables, so
`vpi_put_delays` and `acc_replace_delays` retime it. A later write that
matures at or after the time a pending one would (per bit) ends the pending
write, as a transport delay does, and starts from the value the bit was on its
way to; pulse limits do not apply to foreign writes. The hook sits on the
interpreter's foreign write entry points, below which the interpreter and the
v4 kernel diverge (`deposit_signal`, `deposit_kernel_signal`, `force_signal`,
`release_signal` and the slice forms), and the copies are written through the
same entry points, so both engines behave alike and a loaded design finds the
copies from the copy drivers' processes (nothing new is stored). A `force` and
`release` statement of the design on such a net is not a source either and
reaches the copies the same way (`net_hdl_force`). A net with one delay for
every source needs none of this: its copy follows the net. Not covered: a write
to a net upstream of the load's net through a port connection, a procedural
`assign`/`deassign`, `assign_signal`, and the driver writes of VHPI-created
drivers (`foreign_vpi_net_foreign`).

A delay of zero is not applied (nothing to delay). What else is left undelayed
warns with `FSIM-SDF-SESSION-004`, which says why (the record and its VPI
objects remain): a load no process of whose instance reads the port or whose net
no process outside the instance drives (the delay would be unobservable), a select
outside the port's range, a `NETDELAY` on a net that reaches no port, a driver
that writes the net with an operation outside the dual write (a container
alias, a VITAL `VitalDelay` output), a port connection process that uses an
operation the copy cannot remap, a switch (above), and the port of the design
itself that the design also drives (above). A design without an annotated port keeps
its processes and signals. The copy's driver executes `WriteNetDelay`, an
interconnect delay (IEEE 1364-2005 16.2.4: "twelve transition delays, and
unique reject and error pulse limits are associated with each of the twelve.
An unlimited number of future schedules are permitted"). Each bit's change
takes the delay of its own transition, in the order 0 to 1, 1 to 0, 0 to z,
z to 1, 1 to z, z to 0, 0 to x, x to 1, 1 to x, x to 0, x to z, z to x
(IEEE 1800-2023 30.5.1 Table 30-2), filled in from the delays the SDF gives by
the rules of a module path. Any number of changes of a bit may be pending (a
transport delay: a new change deletes the pending ones that mature at or after
it), and a change that reverses the latest pending one is a pulse as wide as
the time since that change was detected, filtered by the limits of the pending
change's transition (IEEE 1800-2023 30.7): narrower than the reject limit it
is dropped, from the reject limit up to the error limit the pending change
becomes x at its own time and the new change follows from x, and a wider pulse
propagates. A delay without limits has both at the delay, so the write is
inertial. An SDF delay gives limits as a delval of two or three values,
`((delay) (reject) (error))` on `INTERCONNECT` and `PORT` (the error limit
follows the reject limit when only two are given; an empty value gives
none), filled in for the other transitions like the delays. The pending
changes follow one algorithm (`Impl::PendingNetDelayWrites`), which the
interpreter drives through the host's driver state (`Impl::schedule_net_delay`)
and the static kernel through state of its own: the member that writes the copy
runs in the kernel, the copy stays a kernel slot, and each bit's change matures
as a timer of the kernel's heap (`StaticKernel::net_delay_write`), at the
writer's stable order of its time step, in warp mode too. Compiled code calls
one helper for the write (the helper of the operation kind, with no operation
dispatch). A copy that several drivers write (one for each group of sources)
is a resolved kernel slot: the kernel holds each driver's delayed value and
resolves them as the net does. A driver with a drive strength other than the
default is still a host member (the rule for every strength driver), so its
copy stays a host signal. In a run with a
VPI or PLI library, `vpi_put_delays` on the intermodule path or delay device,
and `acc_replace_delays` or `acc_append_delays` on the whole input port,
retime its later transitions (a pending transition keeps its time): the
twelve delays, and with `pulsere_flag` the limits (`Interpreter::
set_net_delay_table`). A record on a select retimes the driver of its run of
bits. `vpi_get_delays` with `pulsere_flag` reads the limits of the record or
of the last put. `PATHPULSE` and `PATHPULSEPERCENT` annotate only specify-path
pulse limits (IEEE 1364-2005 Table 16-1); the pulse limits of an interconnect
delay come from the delval `((delay) (reject) (error))` and the defaults of
16.2.4 and 16.5 above. A delay on one bit of a port that `acc_replace_delays`
writes, and a delay on a port that no annotation created, are not simulated.

The VPI objects follow the same split (IEEE 1364-2005 16.2.4, "references at
or hierarchically after the load shall yield the delayed signal value"). The
object of the load port, its `vpiLowConn`, and every net or port published at
or below the load instance that is the same net (a port of a nested instance
connected to it) read and watch the delayed copy: `vpi_get_value`,
`acc_fetch_value`, `cbValueChange` (it fires when the copy changes) and a
hierarchical name that resolves to such an object. The source side, the
parent's net and the `vpiHighConn` of the port, the output port and the other
loads of the net read the undelayed net. The objects of a design port keep the
net, because the port object is what the outside drives and watches, and the
objects below it read the copy. A `vpi_put_value` deposit or force on a
delayed object writes the copy the instances read (a deposit lasts until the
next change of the net reaches it, a force until its release), not the net;
the drivers of the net, `vpiDriver` iteration and the delay terms stay on the
net. A `$display` or `$monitor` in the load instance reads the copy with the
rest of its processes. A read written with a hierarchical name that lies at
or below a load instance (`$display(top.u1.a)` or `u1.a` in `top`) also reads
the copy, in a process outside the instance too, while that process keeps its
other reads of the net undelayed (`w`): the net and the load port are one
signal, so the lowerer records each read that went through a hierarchical name
(`hierarchical_reads` in the elaborated design: the process, the destination
register of its `ReadSignal`, the signal, the full path the name resolved to
and whether the process waits on its reads) and the split renames exactly the
records whose path lies below a load of that signal (the nearest load instance
wins; the design's own port is excepted as above). A process with an implicit
event list (`always @*`, a continuous assignment) that reads only through the
name waits on the copy, one that also reads the net waits on both. The table is
empty for a design without a hierarchical read, is persisted in the artifact
(runtime schema 102), and so a load applied at simulate time to a loaded
artifact (`simulate --sdf`) finds the same records. Only a whole-name read in an
expression is recorded: an event control on the name (`@(top.u1.a)`), a
select of it, a write or a force of it, and a read inside a memoized callable
keep the net.
They reject other planned target kinds with `FSIM-SDF-SESSION-003` before
publication or live commit. The broader interconnect, drive, pulse, VITAL and mixed-language
APIs described above retain their separate source-tree contracts and tests;
those tests do not establish file-input integration for every family.

Relative file inputs resolve against the process current directory when the
request is configured. The adapter freezes that I/O path; later directory
changes or project loading do not reinterpret it. Reports retain the request's
source identity; CLI paths are normalized to absolute paths during parsing.
That identity participates in session/native keys, so freshly annotating from
a different filename or checkout path can produce different keys. Reloading a
copied annotated snapshot preserves its stored timing and identities without
reading the original file. File bodies are bounded to 64 MiB in aggregate per
request, alongside
the parser's per-file limit. The separate 1-MiB control source-byte limit counts
source/root/cell strings, not file contents.

Before a design is loaded, session configuration may register a pending request
without opening its files. Its summary has no effective annotation. Applying
that request to a design validates and plans all inputs before publishing timing.
A live update uses the existing scheduler safe-point commit and preserves queued
events and timing-check history. After execution starts, a stopped run is not
itself a safe point: live configuration is permitted only while the scheduler
is executing a safe-point callback. Other callback mutation restrictions remain
unchanged. Configuration never advances simulation implicitly. Failed application
retains the prior effective annotation and control state.

Initial annotation contributes to native-cache identity before simulation setup.
Live path/check reannotation updates runtime timing tables and the effective
control generation while preserving process topology and instructions. The
same holds for the net delays: the session plans the cumulative file list on
the baseline design again, so that `INTERCONNECT`, `PORT` and `NETDELAY`
records follow the order of occurrence and `INCREMENT` rules of 16.3, and
`Simulation::commit_sdf_reannotation` takes the planned design when it splits
the same loads as the running one (`sdf_net_delay_retiming`: the same copy
signals and driver processes, a write operation with a table in the same
drivers). The design's records then name the new delays and pulse limits (VPI
reads them), and the delay table of each copy driver is replaced through
`Interpreter::set_net_delay_table`, the mechanism of `vpi_put_delays`: later
transitions use the new table and the ones pending keep their time. A
reannotation that would add or remove a split (a load that had no delay when
the design was elaborated, a delay that becomes zero for a whole load, or a
change in which drivers form a group) cannot change the processes of a
running simulation: it keeps the running split and its delay tables, and takes
the effective path and check timing only (the split below would take such a
load at the safe point, once the engines can grow a running design). Existing
and deferred native modules keep their captured code/cache identities; their
compilation inputs do not contain those mutable timing tables.

### Lazy module input port delays

A foreign `vpi_put_delays` (IEEE 1800-2023 38.32), `acc_replace_delays` or
`acc_append_delays` (IEEE 1364-2005 23.1, `accMapToMipd`) on a whole module
input port that no annotation split gives the port a delay (`vpiMIPDelay`;
owner decision 2026-11-06). Nothing is split in advance: a design elaborated
for a run that loads a VPI or PLI library but never puts a port delay has no
extra signal, process or kernel work. The first put records a request
(`Simulation::request_net_delay_split`, later puts before the split replace its
delays); the next scheduler safe point, the one a live SDF reannotation uses,
makes the split. `plan_sdf_net_delay_graft` runs `split_sdf_net_delays` on the
running design with one `PORT` record, so the copy, its `$mipd_driver`
processes and the renamed reads of the load instance are the ones annotation
builds at elaboration; the processes keep their operation count, so a
suspended process keeps its place, and a single `PORT` record never needs a
dual write. `Interpreter::graft_net_delay` is to give the running engine the
new signal, the driver processes and the rebound reads (the kernel as new slot,
hosted members and rebinding, without the evaluator), with the copy starting at
the net's value then. After the split the port's record is delayed and later
puts retime the copy through `set_net_delay_table`, which both engines honour
(`Interpreter::Impl::net_delay_limits` and `StaticKernel::net_delay_write`);
changes pending then keep their time.

A put on a port bit is a `PORT` record with that bit's select (IEEE 1800-2023
37.14, 38.32; IEEE 1364-2005 23.1): the split delays that bit only, and a later
put on the bit retimes its run's driver. A live SDF reannotation that needs a
split the running design lacks asks for it the same way at its safe point
(`Simulation::commit_sdf_reannotation`), and a load that lost its delay takes a
zero table.

Status: the request, the plan, the bit select, the reannotation wiring and the
retiming are in place. The engines do not yet take the graft: the re-plan entry
point (`StaticKernel::replan_at_safe_point(ReplanRequest)`, with the kinds
`graft_net_delay` and `recompile_all` for the weak-value flip) is declared and
refuses every request, and the evaluator adds no signal or process after start,
so `Interpreter::graft_net_delay` returns false, the port stays undelayed and
`Simulation::net_delay_split_refusals` reports it. `fsim.foreign.vpi-mipd-split`
fails until then. The VPI port and low-connection objects of a late-split port
still name the original signal.

## Command-line control

SDF options are accepted only by phases that elaborate or simulate. `--sdf` is
repeatable; its order establishes file precedence. `--sdf-root` selects the
bound design root, `--sdf-cell` applies a cell glob, `--delay-mode` selects
`min`, `typ` or `max`, and `--sdf-report-limit` bounds detailed report entries.
For example:

```sh
fsim elaborate work.tb --snapshot timed --sdf cells.sdf --sdf-root tb \
  --sdf-cell 'tb.dut.*' --delay-mode max --sdf-report-limit 256
fsim simulate --snapshot timed
```

The same controls are available to workspace `elaborate`, `simulate`, `debug`,
and `tcl` commands. Selector/report options without an
SDF input and SDF options on compile-only phases are rejected.

## Tcl control

`fsim::sdf configure SOURCE ROOT CELL_GLOB min|typ|max REPORT_LIMIT` appends one
file input and atomically publishes the resulting pending or effective control.
With a loaded design, configuration applies the selected timing before success.
`fsim::sdf status` returns the effective input/file/path/check counts, generation,
truncation state and semantic identity. `fsim::sdf report` returns the bounded
input/path/timing-check detail list. Summary and report reads are safe inside a
simulation callback. After execution starts, configure is allowed inside a
callback only at an actual scheduler safe point; direct post-run configuration
is rejected without replacing the effective annotation. Tcl uses the
`fsim::add_callback safe_point` callback with `{time delta phase}` arguments; the scheduler
must actually be in its safe-point dispatch.

```tcl
set summary [fsim::sdf configure cells.sdf tb {tb.dut.*} max 256]
puts "SDF generation [dict get $summary generation]"
foreach entry [fsim::sdf report] {
  puts "[dict get $entry kind] [dict get $entry object]"
}
```

Failed duplicate, phase, limit or resource validation retains the last
published request and reports a cataloged `FSIM-SDF-CONTROL-*` diagnostic.

## Native C and C++ APIs

The installed C API in `fsim/api.h` adds append-only
`fsim_sdf_input_t`, `fsim_sdf_options_t`, `fsim_sdf_summary_t` and
`fsim_sdf_report_entry_t` structures. Configure with
`fsim_session_configure_sdf`, read the immutable summary with
`fsim_session_get_sdf_summary`, and enumerate bounded report entries with
`fsim_session_get_sdf_report_entry`. Callers advertise every structure prefix
with `struct_size` and `FSIM_API_VERSION`; invalid prefixes, enums, views,
indices and phase transitions fail without replacing prior state. At this session
boundary, `fsim_sdf_input_t.source_identity` is a file name. It follows the
configuration-time current-directory rule above; pending registration preserves
the supplied identity and defers reading until a design is available. The
low-level C++ control and IR APIs continue to accept their own source identities.
For live updates, `fsim_callbacks_t.safe_point_info` at
`FSIM_SCHEDULER_PHASE_POSTPONED` provides the scheduler-safe callback boundary.
Other mutation APIs retain their existing running/callback restrictions.

The source-tree C++ surface is split by ownership:

- `fsim/frontend/sdf.hpp` parses and normalizes exact SDF input;
- the `sdf_*resolution.hpp`, `sdf_mapping_validation.hpp` and
  `sdf_target_plan.hpp` application headers bind stable targets;
- the value, path, interconnect, delay-mode, timing-check, condition, pulse,
  precedence, scheduling, drive and reannotation headers own effective timing;
- `sdf_control.hpp` owns CLI/Tcl/C/C++ control summaries and reports;
- `sdf_effective_archive.hpp` owns schema-1 `FSDFEFF` object, design, library,
  native-cache and checkpoint payloads; and
- `sdf_observability.hpp` exposes stable debugger, callback, internal-trace,
  VPI and VCD timing/violation records;
- `sdf_vital_*` and `sdf_mixed_*` headers own structural VITAL models,
  scheduling/checks, mixed boundaries, foreign interfaces and observation (a
  planning layer that SDF file input does not call; see above);
- `sdf_vital_archive.hpp` retains effective VITAL/mixed state in every portable
  form; and
- `sdf_vital_phases.hpp` gives project, CLI, Tcl, C, C++ and non-project
  phases equivalent summaries, archive identity and failures.

Every public result reports success only when it has no error diagnostic.
Callers can tighten default resource and report limits but cannot widen a
target or accept a partial mutation.

## Persistence and observability

The existing `FSDFPORT` payload retains normalized and resolved annotation
identity. The `FSDFEFF` payload adds exact source and effective values, target
identities, selected policy and provenance. Objects, designs, mapped libraries,
cold/warm/relocated native caches and checkpoints reject bad magic, unsupported
schema, truncation, trailing bytes, corruption and expected-identity mismatch
before publication. A successful relocated reload does not consult the
original SDF path.

Effective paths, timing checks and violations have stable debugger IDs, VPI
handles, VCD names, values, source spans, time, delta, scheduler region and
source order. Debugger, callbacks, internal trace, VPI and VCD observe the same
preallocated records without changing scheduling.

## Governed evidence

The retained source-tree application corpus is governed by the seventeen-row
[`sdf_application_inventory.tsv`](../tests/feature_matrix/sdf_application_inventory.tsv)
maps Changes 2-18 one-to-one and is checked by
`fsim.sdf-application-inventory`. The `fsim.sdf-application-closure` test runs
seven retained-log stages and requires the exact
`FSIM-SDF-APPLICATION-CORPUS-PASS` transcript tokens independently of child
exit status. Its owned standard-cell, primitive, interconnect, pulse and timing-
check corpus covers interpreter/LLVM, optimized/debug, project/non-project,
object/design/library/cache/checkpoint, relocation/replay, public observation,
Linux/Windows contracts and cataloged negative/resource families.

See the [diagnostic catalog](diagnostics.md#standard-delay-format-frontend),
[feature matrix](feature-matrix.md) and
[Batch 169 application audit](v2-sdf-application-release-audit.md),
[Batch 170 SDF/VITAL audit](v2-sdf-vital-release-audit.md) and
[mixed VITAL example](../examples/sdf_vital_mixed/README.md) for the frozen
boundaries and executable evidence.
