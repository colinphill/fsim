<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim

`fsim` is a greenfield mixed-language HDL simulator written in C++20. Its
intended compilation path is:

```text
VHDL / Verilog / SystemVerilog / SystemC
                    |
          typed, source-aware IR
                    |
       elaborated design + typed SimIR
              /                 \
 reference interpreter       LLVM ORC JIT
              \                 /
       deterministic runtime, debugger, VCD/FST
```

## Project status

This repository is an **internal vertical slice**, not the fsim v1 release.
It establishes the semantic and platform spine on which the full language
implementations will be built.

Verilog and SystemVerilog bit strings and based-number literals have no
implementation-selected language width limit. Source- and context-determined
width, signedness, and `X`/`Z` planes remain exact through parsing, folding,
execution, VPI/public values, traces, artifacts, relocation, and caches. A
host-addressability boundary or configured memory/work/trace ceiling is a
physical resource limit, never a Verilog legality rule, and fails with a
distinct checked diagnostic rather than narrowing the value. See the
[Verilog-2005 guide](docs/verilog-2005.md) and its
[closure audit](docs/verilog-2005-closure-audit.md).

The current tree contains:

- explicit VHDL-87, VHDL-93, VHDL-2000 and VHDL-2002 source modes alongside
  the VHDL-2008 default, with revision-correct syntax, predefined environments,
  dependencies, artifacts, caches, relocation and interpreter/LLVM execution;
- compiler-owned, explicitly non-standard `ieee.std_logic_signed`,
  `std_logic_unsigned`, `std_logic_arith` and `std_logic_misc` compatibility
  packages with exact revision/source provenance, arbitrary-width operations,
  null/direction behavior and deterministic mixed-package ambiguity rejection;
- C++20 value kernels for packed 2-, 4-, and 9-state logic, including governed
  arbitrary-width SystemVerilog Logic4 constants and runtime values;
- a deterministic, single-thread, phased event scheduler;
- a typed SimIR and reference interpreter;
- normalized Verilog specify module paths, pulse controls, all twelve timing
  checks, and shared interpreter/LLVM scheduling with portable artifact state;
- clean-room SDF 4.0 parsing with explicit SDF 2.1/3.0 adapters, exact
  decimal/triple normalization, deterministic mixed-language hierarchy and
  endpoint resolution, checksummed persistence, and Verilog/SystemVerilog
  plus VHDL/VITAL and mixed-language timing application, portable artifacts,
  bounded foreign/debug observation and equivalent public phase controls;
- hand-written VHDL-2008 and Verilog/SystemVerilog tokenizers and parsers for a
  deliberately small executable subset;
- bounded VHDL package declarations with declaration-ordered scalar
  integer/Boolean/bit constants, explicit `use library.package.all` or
  `use library.package.name` visibility, direct `package.constant` and
  `library.package.constant` expressions, acyclic package-to-package imports,
  and precise transitive package-source specialization provenance;
- bounded VHDL context declarations/references with recursive reusable
  library/use visibility, cycle diagnostics, and transitive context-source
  cache provenance;
- package-, entity-, and architecture-declared VHDL subtypes over scalar
  logic/bit/Boolean, constrained signed/unsigned and logic/bit vectors,
  portable integer ranges, and bounded records; chained/use/direct-name
  visibility; derived-constraint legality; specialization-dependent packed
  bounds; subtype-typed constants/generics; entity-to-architecture visibility;
  range-safe hierarchy aliases; and interpreter/LLVM O0/O2, debugger-local,
  VCD, and transitive native-cache equivalence;
- package-, entity-, and architecture-declared one-dimensional VHDL array
  types over scalar bit/Boolean/std_logic/std_ulogic or visible scalar
  subtypes, with constrained and `integer`/`natural`/`positive range <>`
  forms, nominal assignment and hierarchy checks, whole/index/slice reads and
  writes, contextual positional/index/range/choice-list/`others` aggregates,
  object/type/subtype bounds attributes and direction-aware
  `range`/`reverse_range` loops, and signed-32-bit dynamic single-element
  reads/writes with exact declared-range mapping,
  explicit mixed-language wrapper enforcement, and interpreter/LLVM O0/O2,
  debugger, VCD, and cache equivalence;
- architecture-local or project-package, non-nested VHDL record types with
  case-insensitive scalar/packed logic, bit, and Boolean elements;
  use-clause or selected-name visibility; reusable same-language entity ports;
  declaration-order flattened layouts; exact element defaults; whole-record
  copies/equality; contextually typed positional, named, and final-`others`
  aggregate literals in initializers, assignments, comparisons, and
  conditional alternatives; constant member/index/slice reads and writes;
  debugger-local visibility; and interpreter/LLVM O0/O2, transitive cache,
  and VCD equivalence;
- entity-level VHDL-2008 unclassified interface type generics with
  same-language named/positional constrained subtype actuals over supported
  vectors, portable integers, nominal enumerations, records, and
  one-dimensional scalar-element arrays; nested forwarding, targeted
  constraint/object diagnostics, dependent object specialization, retained
  child-source metadata, and interpreter/LLVM O0/O2 selective-cache
  equivalence;
- bounded VHDL-2008 interface function generics with retained pure scalar
  profiles, required/named/box defaults, local and directly imported package
  actuals, function-derived constant defaults, nested forwarding, runtime
  calls, package-body/source provenance, debugger call points, and
  interpreter/LLVM O0/O2 cold/warm/edit-cache equivalence;
- bounded VHDL-2008 interface procedure generics with retained constant or
  variable scalar profiles and `in`/`out`/`inout` modes, required/named/box
  defaults, local and directly imported package actuals, nested forwarding
  and procedure calls, deterministic ordered copy-in/copy-out to signals or
  variables, live debugger formals/locals, package-body/source provenance,
  and interpreter/LLVM O0/O2 cold/warm/edit-cache equivalence;
- bounded VHDL-2008 interface package generics and entity/architecture-local
  generic package instances over the existing value, type, function, and
  procedure families; explicit/default/box maps, declaration-ordered
  specialization, selected constants/types/subprograms, nested forwarding,
  exact instance and transitive source identity, debugger-visible procedure
  frames, and interpreter/LLVM O0/O2 cold/warm/edit-cache equivalence;
- bounded VHDL-2008 generic function and procedure templates and local or
  package-visible instantiations over the existing value, type, function, and
  procedure generic families; explicit/default/box maps, declaration/body
  matching, dependent callable specialization, use as nested interface
  subprogram actuals, versioned transitive identity, debugger call metadata,
  and interpreter/LLVM O0/O2 cold/warm/edit-cache equivalence;
- bounded VHDL-2008 configuration declarations and architecture declarative
  configuration specifications over component-style instances; recursive
  static block and selected for/if/case-generate rules, explicit label, `all`,
  and `others` selection, entity/configuration/open binding aspects, nearest-
  scope precedence, named generic/port-map composition, configuration top
  selection, referenced-subtree activation, direct-entity isolation, versioned
  transitive source identity, and interpreter/LLVM O0/O2 cold/warm/edit-cache
  equivalence;
- bounded VHDL-2008 component declarations in architecture, entity, package,
  block, and selected-generate regions with retained value/type/function/
  procedure/package generics plus scalar/vector, enumeration, named-subtype,
  non-nested record, and one-dimensional scalar-element array port profiles;
  explicit/omitted/box generic actuals, declaration-ordered dependent-port
  specialization, lexical/package visibility, nominal/profile-aware overload
  selection, formal-aware named/positional associations, latest-analyzed
  same-library architecture default binding, configuration-map precedence,
  whole-signal composite execution, statically foldable component input
  defaults, explicit or omitted open output-family ports, declaration-visible
  scalar/vector/enumeration/record/array aggregate defaults, required direct-
  entity input enforcement, and version-5 normalized-default/open/mapping
  identity with interpreter/LLVM O0/O2 cold/warm/edit-cache equivalence;
- bounded SystemVerilog packages with immutable integral
  parameters/localparams, packed integral typedef aliases, and packed enum
  types/enumerators plus non-nested packed struct and equal-width packed union
  types with executable member, constant member-select, and constant
  `+:`/`-:` indexed-select reads/writes plus checked constant replication
  concatenations, signed/unsigned arithmetic shifts, fixed-width
  exponentiation, complemented unary reductions, and both binary XNOR
  spellings; wildcard or selected imports;
  direct
  `package::constant`/`package::type` references; recursive dependency
  diagnostics; and precise specialization provenance;
- bounded SystemVerilog module/package functions with explicit `automatic`
  lifetime, arbitrary-width packed integral value arguments/locals/results,
  parameter-sized
  types, function-name or explicit-return results, constant evaluation,
  package visibility, nested nonrecursive calls, debugger safe points, and
  interpreter/LLVM O0/O2 plus native-cache equivalence;
- bounded SystemVerilog module/package tasks with explicit `automatic`
  lifetime, arbitrary-width packed integral input/output/inout formals,
  parameter-sized
  types, deterministic copy-in/copy-out, local/imported/package-selected
  nested nonrecursive calls, delays/event/condition waits, deferred copy-out,
  post-suspension early return, debugger stop/resume locals and safe points,
  and interpreter/LLVM O0/O2 plus native-cache equivalence;
- bounded Verilog-2005/SystemVerilog `$clog2` folding for nonnegative integral
  constant arguments in parameter/localparam defaults and specialized packed
  ranges, with zero and exact/non-power-of-two edge behavior;
- Verilog/SystemVerilog preprocessing with quoted/angle includes, manifest/CLI
  macros, object/function expansion with default arguments, multiline
  replacements, token concatenation/stringification, conditional compilation,
  source ancestry, `file`/`source-set`/`combined` state-sharing policies, and
  compilation-unit-wide cache provenance;
- bounded Verilog/SystemVerilog `` `timescale`` context plus SystemVerilog
  compilation/module `timeunit`/`timeprecision`, exact fractional/scientific
  and explicit-unit delays, independently selected `min:typ:max`
  rise/fall/turnoff lists, precision-first half-up rounding, automatic
  selection of the finest declared precision, and inertial whole/slice
  continuous writes with packed transition selection and pulse rejection;
- executable scalar `` `default_nettype`` implicit nets plus
  reset/cell/keyword-version/unconnected-drive compiler state, including
  cell specialization metadata and pull initialization for omitted inputs;
- bounded scalar `buf`/`not`/`and`/`nand`/`or`/`nor`/`xor`/`xnor` gate
  primitives with shared one/two-value transition delays and comma-separated
  instances, lowered through the common continuous-process path;
- Verilog-2005 combinational and sequential user-defined primitives with
  ordered four-state/edge tables, optional initial state, static instance
  arrays, common inertial transition delays, logical-library resolution, and
  interpreter/LLVM/artifact/debugger/VCD parity;
- Verilog-2005 drive, pull, supply, and charge strengths with strength-aware
  four-state resolution across continuous, gate, UDP, procedural, VHDL, and
  SystemC drivers;
- executable MOS/resistive-MOS and cycle-safe bidirectional/conditional
  transmission primitives, plus `trireg` retention and checked finite,
  zero, or infinite decay, with interpreter/LLVM/artifact/debugger/VCD parity;
- recursive VHDL/SV/SystemC instance elaboration in every hierarchy direction
  with automatic parent-plus-configured-library resolution, repeatable
  `--search-library` overrides, explicit qualified overrides, whole-signal port
  aliasing, and boundary validation;
- specialization-time VHDL and SystemVerilog conditional/iterative/selection
  executable generate expansion, unguarded VHDL block statements, and
  explicit or implicit SystemVerilog generate forms with labeled and indexed
  generated scopes retained in mixed binding paths;
- lowering of scalar and common packed operations into SimIR;
- bounded source-level VHDL bare, `on`, `until`, and `for` wait clauses,
  including event-or-timeout combinations, plus Verilog/SystemVerilog
  integer-delay, any-change, `posedge`, and `negedge` procedural event
  controls;
- Verilog/SystemVerilog named-event declarations, immediate `->` triggers,
  SystemVerilog update-phase or delayed `->>` triggers, and repeated
  static/dynamic event-control wakeups;
- SystemVerilog multidimensional unpacked selections, recursive unpacked
  struct/union and string container values, canonical string associative
  indices, typed mailboxes and semaphores, generation-safe process handles,
  runtime scalar delays, general edge expressions, runtime-selected
  force/release, and deterministic `shuffle()` through interpreter and LLVM
  service paths, portable artifacts, relocation, and native-cache reuse;
- Verilog-2005/SystemVerilog literal and empty `$display`, `$write`, and
  postponed `$strobe` routed through CLI/Tcl output with newline/no-newline
  semantics, decoded control/quote/backslash/octal escapes, and
  interpreter/LLVM O0/O2 equivalence;
- value-sensitive `$monitor` registration for direct packed signals, with
  coalesced committed-value publication in the postponed phase and
  `$monitoron`/`$monitoroff` control;
- constant unsigned decimal/binary/octal/hex numeric output arguments folded
  to their default decimal text before SimIR lowering;
- declared-width signed based output literals interpreted as two's-complement
  decimal text;
- runtime `$display`/`$write` `%b`/`%h`/`%x`/`%o`/`%d`/`%c`/`%s` formatting
  for ordered packed expressions, including uppercase aliases, four-state
  bits, literal text, `%%`, minimum widths, left/zero padding, and
  `%0b`/`%0h`/`%0o` leading-zero suppression;
- default-decimal additional runtime operands plus `%m` elaborated hierarchy
  and `%t` current-tick substitutions;
- formatted `$strobe` captures its evaluated value in the active phase and
  publishes the resulting text in the postponed phase;
- decoded Verilog/SystemVerilog literal escapes in `$info`, `$warning`,
  `$error`, and `$fatal` diagnostics;
- VHDL-2008 literal `report` statements at all standard severities with
  retained source metadata, interpreter/LLVM equivalence, native API
  assertion-callback visibility, and callback-before-stop `failure`;
- deterministic simple-expression sensitivity inference for `always @*`,
  time-zero `always_comb`/`always_latch`, and dynamic `@*`;
- ordered Verilog/SystemVerilog `case`/`casez`/`casex`/`default` lowering
  with comma-separated choices, exact four-state matching, and distinct
  selector-or-choice `Z` versus `X`/`Z` wildcard policies;
- SystemVerilog `==?`/`!=?` expression lowering with right-operand-only
  `X`/`Z` masking and unmasked left-side unknown propagation;
- SystemVerilog `$signed`/`$unsigned` casts that preserve packed bits and width
  while selecting signed or unsigned comparisons and arithmetic shifts;
- SystemVerilog `$isunknown` detection across complete packed `X`/`Z` values;
- SystemVerilog `$bits` queries for statically sized packed expressions;
- SystemVerilog `$left`/`$right`/`$low`/`$high`/`$size`/`$increment`
  queries for one-dimensional packed objects with ascending or descending
  ranges and an optional constant dimension `1`;
- SystemVerilog `$dimensions`/`$unpacked_dimensions` queries for bounded
  packed objects;
- SystemVerilog `$onehot`/`$onehot0` packed bit-stream queries with exact
  known-bit results across `X`/`Z` elements;
- SystemVerilog `$countones` with a compact arbitrary-width SimIR operation;
- SystemVerilog `$countbits` with constant exact-state control masks;
- nested VHDL `if`/`elsif`/`else` with Boolean typing and nested
  Verilog/SystemVerilog `if`/`else` with packed four-state truth conversion;
- VHDL Boolean literals, equality/inequality, and `not`/`and`/`or`/`xor` plus
  `nand`/`nor`/`xnor` Boolean operations;
- labeled or unlabeled VHDL concurrent assertions lowered to stable reactive
  processes with inferred condition sensitivity and retained diagnostics;
- SystemVerilog-2017 sequence/property/checker and concurrent
  assert/assume/cover/restrict ownership, with a bounded executable scalar
  property slice providing clock-edge scheduling, controls, callbacks,
  debugger/trace events, per-instance coverage, multiple roots, and portable
  artifact parity;
- SystemVerilog-2017 functional coverage with design-unit- and class-owned
  covergroups, scalar/ranged/wildcard/array/transition bins, automatic and
  explicit crosses, guards and exclusions, options/goals/percentages,
  explicit/event/procedural sampling, callbacks, structured reports,
  debugger/trace projections, governed resource limits, and portable
  object/design/library plus interpreter/LLVM O0/O2 parity;
- bounded SystemVerilog-2017 DPI-C imports/exports with exact owner/profile
  retention, scalar/composite/open-array marshalling, simulation-owned scopes,
  callbacks and suspending tasks, plus versioned portable C/C++ plug-ins with
  transactional ABI/symbol checks, content provenance, and leased lifetime;
- governed SystemVerilog VPI with generation-qualified hierarchy/value
  handles, callbacks, control, system callables, portable I/O, transactional
  save/restart, and versioned C/C++ plug-ins whose native state is explicitly
  retained or invalidated across restart and artifact flows;
- governed unmodified UVM 1.2 and UVM 2020-3.1 source entry points plus a
  simulation-owned object/component, deterministic line/tree/table printer and
  deep/shallow/reference comparer policy, registry/factory,
  resource/configuration, command-line, report, phase/domain, objection/drain,
  activity/debug, TLM1, TLM2, sequence/sequencer,
  driver/monitor/agent/scoreboard, callback/transaction, register-model,
  foreign snapshot, and portable-checkpoint boundary. Interpreter, LLVM O0/O2,
  debug, multiple-root, callback, VCD/FST, cold/warm cache, and portable
  relocated artifact paths agree under explicit resource bounds; typed endian-
  aware object packing, transaction object recording, deterministic replay,
  event pools, barriers, generic pools/queues, heartbeats, and spell challenges
  are closed. Its simulation-isolated command-line processor retains ordered
  argv and supplies bounded plusarg/UVM, exact/prefix, duplicate-value, and
  tool/version queries. Simulation-owned `run_test` applies deterministic test,
  seed, and timeout precedence, prints bounded topology, and cleans repeated,
  finished, fatal, or timed-out runs. Hierarchical report controls apply
  source-ordered phase/time verbosity, max-quit, severity/action/file and
  catcher policy, while bounded objection tracing preserves stable escaped
  records across engines. Factory/config/resource tracing supplies stable
  escaped resolution/access records, usage inventories, contained callbacks,
  and matching debugger/activity views across engines. Untouched UVM 1.2
  legacy field, utility/registry, sequence, callback, analysis-implementation,
  and report macros retain their generated signatures and factory/portable
  metadata across the same paths. Its phase, objection, TLM, sequence,
  callback, register, policy, and command-line classes retain exact legacy
  method profiles, while default deprecated package/component/test-done and
  sequence/sequencer aliases plus their disabled and malformed diagnostics are
  governed explicitly. UVM 2020.3.1 policy/copier/field-operation, long-integer
  packing, printer/comparer/packer/recorder, reporting, version, retained-
  compatibility, and removed-API differences are frozen independently;
  explicit manifest/CLI release selection normalizes the complete difference
  dispatch and retains release plus exact source identity through objects,
  native caches, designs, schema-2 checkpoints, and replay while rejecting
  mixed or mismatched state transactionally. An explicit project-owned core
  smoke source binds the governed object-policy, factory, resource/config,
  command-line, report, callback, and test-runner surfaces and retains all
  eleven category method bodies through direct and portable object/design
  execution for both releases. A retained flow inventory additionally freezes
  phase callbacks, TLM1 members, sequence/role/callback inheritance, and the
  common TLM2 payload API before the two-release interpreter/LLVM O0/O2,
  cold/warm-cache, debug, multiple-root, cancellation, and replay matrix. The
  retained register inventory also freezes project register roles and common
  standard field/map/memory profiles, then exercises little/big-endian maps,
  byte enables, adapters, predictors, frontdoors, mixed VPI/VHPI backdoors,
  standard sequences, callbacks, coverage, DPI callbacks, negative operations,
  relocated checkpoints, and retained-record caps across the same paths. The
  exact runner is filesystem-neutral and freezes the x64 C/cdecl foreign ABI;
  every stage has a 1,200-second wall limit, every process has a cross-platform
  6 GiB address-space limit, and each serial release matrix has a 7,200-second
  limit. An authoritative supported-boundary inventory assigns positive,
  negative, and execution owners to 17 active families per release and rejects
  any missing retained identity among 53 UVM 1.2 or 56 UVM 2020-3.1 governed
  classes and all 27 project classes, with no supported-failure waiver path;
  a 21-row executable release-closure matrix additionally freezes every public
  compatibility switch, retained resource/trace baseline, diagnostic/source/
  complexity audit, artifact/cache provenance, and zero unresolved supported
  gaps. Exhaustive conformance beyond this documented boundary remains later
  work;
- governed VHDL-2008 VHPI with selected/indexed hierarchy, scalar/composite/
  nine-state values, drivers, callbacks, foreign models, associations,
  root-isolated reporting, transactional restart/remap, and independently
  compiled versioned C/C++ images;
- governed embedded PSL with 29 supported VHDL-2008/PSL inventory rows, zero
  unresolved active rows, deterministic attempt/coverage/debug/VCD equality
  through interpreter and LLVM O0/O2 cold/warm execution, source-independent
  object/library/design replay, mixed roots, a 6 GiB process ceiling, and an
  installed [`fsim-vhdl` tutorial](docs/vhdl-psl-tutorial.md); see the
  [support boundary](docs/vhdl-psl.md) and
  [closure audit](docs/vhdl-psl-closure-audit.md);
- ordered VHDL sequential packed `case` statements with `|` choices and
  `others`, lowered through common exact case-equality branches;
- concurrent and sequential VHDL-2008 conditional assignments with chained
  Boolean `when`/`else` alternatives;
- VHDL `with`/`select` concurrent signal assignments with grouped exact
  choices, a final `others`, inferred sensitivity, and optional waveform delay;
- ordered VHDL sequential, conditional-concurrent, and selected signal
  waveforms with per-element exact integer time, `unaffected` alternatives,
  implicit/explicit inertial, transport, optional rejection limits, and
  atomic per-scalar projected transactions for whole or constant-slice
  targets;
- VHDL packed-object `'left`, `'right`, `'low`, `'high`, `'length`, and
  `'ascending` attributes with declared-direction preservation;
- VHDL signal `'event` with delta-scoped effective-value-change semantics and
  the classic `clk'event and clk = '1'` clock idiom;
- VHDL signal `'last_value`, preserving the packed effective value immediately
  before the latest value-changing event;
- VHDL signal `'last_event` as elapsed global-resolution ticks, with
  `TIME'HIGH` before the first event;
- VHDL signal `'last_active` as elapsed time since the latest transaction;
- zero-duration and static-duration VHDL signal `'stable`/`'quiet`, plus
  interned typed `'transaction` and `'delayed` implicit signals in expressions,
  process sensitivity lists, and waits;
- VHDL signal `'active` with transaction semantics distinct from
  value-changing `'event`;
- process-relative VHDL signal `'driving` and typed `'driving_value` queries;
- bounded SystemVerilog conditional-expression lowering with exact
  four-state unknown-condition bit merging;
- SystemVerilog procedural arithmetic/bitwise/shift compound assignments and
  standalone prefix/postfix increment/decrement, including constant-selected
  packed targets;
- SystemVerilog `final` procedures executed exactly once after quiescence or
  `$finish`, with interpreter/LLVM lifecycle equivalence;
- resumable Verilog-2005/SystemVerilog `$stop`, preserving the next statement
  and deferring final procedures until resumed completion;
- SystemVerilog `$info`, `$warning`, `$error`, and `$fatal` as standalone
  tasks or immediate-assertion actions, including simple and lexical-block
  pass/failure actions with severity/source retention;
- vector-aware SystemVerilog logical negation and unsigned
  equality/relational comparisons with four-state unknown propagation, plus
  exact known-result `===`/`!==` comparison of `0`/`1`/`X`/`Z`;
- mixed-width SystemVerilog logical conjunction/disjunction with controlling
  known-value and four-state indeterminate semantics;
- SystemVerilog unary reductions (including `~&`, `~|`, `~^`, and `^~`),
  binary `~^`/`^~` XNOR, and mixed-width logical shifts, including four-state
  reduction rules and deterministic unknown/oversized shift handling;
- fixed-width signed and unsigned packed arithmetic/comparison for VHDL and
  SystemVerilog, including distinct VHDL `rem`/`mod`, SystemVerilog
  mixed-signedness rules, deterministic `X/Z`/zero-divisor behavior, and
  fixed-width signed-overflow wrapping, plus bounded packed VHDL signed
  `abs`;
- VHDL packed `sll`/`srl`/`sla`/`sra` shifts and `rol`/`ror` rotates for
  locally static or dynamic base-`integer` counts, including standard
  negative-count direction reversal and four-state arithmetic fill;
- bounded signed 32-bit two-state VHDL `integer`, `natural`, `positive`, and
  explicit `integer range ... to|downto ...` ports, signals, and process
  variables, with subtype-left initialization, range-safe hierarchy aliases,
  and checked unary/arithmetic/division/power/subtype-store failures shared by
  the interpreter and LLVM O0/O2;
- declared-range-aware SystemVerilog constant bit/part selects and packed
  concatenations plus signed-32-bit dynamic single-bit reads/writes,
  including ascending and non-zero-based source ranges;
- declared-range-aware VHDL indexed names/slices and correct width-summing
  VHDL `&` concatenation for packed scalar/vector operands;
- constant bit/part assignment targets for SystemVerilog and VHDL packed
  signals and procedural locals, including blocking, common-update, and
  delayed partial writes with stable source-order merging;
- dynamic single-element assignment targets for SystemVerilog and VHDL
  packed signals and procedural locals, including execution-time index
  capture for blocking, NBA/update, inertial, delayed, and projected writes;
- locally static VHDL sequential `for` loops in either `to` or `downto`
  direction, including null ranges and loop-indexed packed selections,
  elaborated into deterministic source-order SimIR;
- bounded SystemVerilog procedural `for` loops with inline `int`/`integer`
  indices, canonical unit-step conditions/updates, exclusive or inclusive
  bounds, and the same deterministic common-loop lowering;
- locally static Verilog/SystemVerilog `repeat (COUNT)` statements with
  explicit zero-count behavior and bounded common-loop lowering;
- executable VHDL/Verilog/SystemVerilog `while` backedges plus suspending
  Verilog/SystemVerilog `forever` loops, with language-specific condition
  truth rules and statement safe points on every iteration;
- nested SystemVerilog `break`/`continue` and VHDL `exit`/`next`, including
  conditional VHDL forms, with innermost-loop control across both statically
  unrolled and runtime loops;
- VHDL opening/end loop labels plus `exit label` and `next label`, including
  control transfers from runtime inner loops to static outer loops;
- unconditional VHDL sequential loops and post-test SystemVerilog `do-while`
  loops, including correct trailing-condition targets for `continue`;
- first-suspending VHDL `wait until` and immediate-test
  Verilog/SystemVerilog `wait (expression)` suspension with condition
  re-evaluation, attached procedural statements, and non-polling permanent
  waits;
- combined VHDL `wait on ... until ... for ...` event/timeout races with
  absolute-deadline preservation across false condition wakeups;
- a narrow LLVM ORC adapter for processes whose value-bearing operations are
  at most 64 bits, including explicit jumps/branches and caller-owned
  resumable frames for timed, dynamic-signal, and static-sensitivity waits,
  yields, design stop, loops containing suspension points or per-iteration
  source safe points, update-phase and delayed writes, and a shared checked
  allocation-free single-word `Logic4` `aval`/`bval` and four-plane `Logic9`
  paths into the simulation kernel; eligible processes owned by
  one bounded elaborated specialization are lowered and optimized together in
  one LLVM module while capability misses retain per-process fallback;
- a checksummed persistent object-cache primitive with process-aware per-key
  locking, atomic replacement, stale-lock recovery, hit-refreshed LRU metadata,
  active-lock-safe age/count/encoded-byte pruning, stale-temporary cleanup, and
  LLVM native-object reuse plus cold/warm/prune telemetry beneath the
  configured application cache;
- buffered VCD output;
- a schema-1 project-manifest loader and command-line driver;
- an executable versioned C session API for build, hierarchy/value access,
  simulation control, and synchronous callbacks;
- versioned SystemC plug-in ABI, dynamic-library loading, typed factory
  construction, and foreign-child registration;
- an fsim SystemC compatibility header plus a shell-free, cached host compiler
  for plug-in shared libraries, with build-time entry-point and factory
  validation;
- bounded `sc_logic`, arbitrary-width `sc_bv`/`sc_lv`, and 1–64-bit
  `sc_uint`/`sc_int` operations with checked bit selection, four-state
  propagation, shifts, reductions, and wrapping integer arithmetic;
- executable facade-defined `SC_METHOD` processes with time-zero
  initialization, `dont_initialize()`, static any-change/edge sensitivity,
  dynamic time/event `next_trigger`, immediate/delta/timed named events with
  pending replacement/cancellation, strict `notify_delayed`, dynamic OR/AND
  event expressions, registered primitive-channel update callbacks,
  kernel-backed module-local `sc_signal` objects, canonical packed port reads,
  and common update-phase writes.

The implemented SystemC hierarchy spine is bidirectional: HDL instances may
bind to typed SystemC factories, and those factories may declare
elaboration-time foreign children explicitly bound to VHDL or SystemVerilog
targets. Either HDL or SystemC may be the selected top. The resulting ports,
aliases, HDL descendants, and stable SystemC instance metadata enter the common
elaborated design. Static `SC_METHOD` callbacks and deduplicated
`sc_prim_channel::request_update()` callbacks execute in that hierarchy;
module-local `sc_signal` objects use the common signal store and scheduler.
Typed `sc_in`/`sc_out`/`sc_inout` bindings to those signals alias one common
DesignIR object, including when HDL instantiates the SystemC module. Native
SystemC child members now elaborate recursively with stable hierarchy handles
and direct-parent signal or port binding. The four standard module lifecycle
callbacks run at deterministic build/start/terminal boundaries. Standard
`sc_signal_in_if`/`sc_signal_inout_if` exports retain hierarchy metadata while
resolving to common signals. `SC_THREAD` and `SC_CTHREAD` use single-threaded
Boost.Context fibers for timed, event, and static-sensitivity suspension.
General custom interfaces remain planned.
Facade modules declare VHDL/SV children with the typed
`fsim::systemc::hdl_instance` extension; the full child path in `fsim.toml`
selects the implementation, so mixed hierarchy remains explicit in both
directions. Bounded scalar VHDL generic and integral SystemVerilog parameter
actuals already transfer across explicit VHDL/SV bindings in either direction
before port widths are checked. `hdl_instance::set_actual` also transfers
immutable named scalar values from SystemC into a selected VHDL/SV child;
typed factory schemas also carry the reverse HDL-to-SystemC direction for the
bounded scalar subset. The common hierarchy walk evaluates source-language
actuals, exposes validated canonical values to constructors, and only then
checks parameter-dependent ports.

VHDL `if`/`else generate` and SystemVerilog `generate if` instance branches
are evaluated per specialization; selected block labels form stable hierarchy
components, including for explicit bindings to or from SystemC factories.
VHDL integer-range `for generate` and SystemVerilog loops using inline or
module-scope `genvar` declarations use deterministic `label[index]`
components and substitute the loop constant into child construction actuals
before specialization. Canonical assignment, prefix/postfix increment or
decrement, and compound-add/subtract updates normalize to the same HIR.
VHDL `case generate` supports scalar and inclusive locally static `to`/
`downto` range choices, while SystemVerilog generate-case supports scalar
constant choices. Both select default alternatives per specialization and
retain the alternative label in explicit mixed-language binding paths.
Selected generated bodies can execute local packed signals, concurrent
assignments, and processes. Their local names are scope-qualified for
debug/VCD visibility, including independent `label[index]` objects for each
realized loop iteration.
Bounded scalar/integral VHDL constants and SystemVerilog
parameters/localparams in generated bodies are evaluated in declaration order
after specialization and loop-index substitution, then folded before SimIR.
Unguarded labeled VHDL block statements always elaborate their body into the
declared block scope. SystemVerilog conditional/iterative/selection generates
may use their standard implicit module-item forms, while direct declarations and
behavior plus named static `begin : label` blocks inside explicit
`generate`/`endgenerate` regions elaborate through the same common hierarchy.

The full v1 language coverage described in
[Language support](docs/language-support.md) is not implemented yet. In
particular, complete semantic analysis, general mixed-boundary conversions,
wired-net
resolution beyond `sv_wire`, complete VHDL generic typing and SystemVerilog
parameter typing, complete scoped/local type coverage and call
safe points, broader interpreter/JIT differential coverage, remaining SystemC
kernel behavior, parameterized/nonconstant and multiple rise/fall/turnoff
delays, general VHDL package declarations/bodies and IEEE packages, complete
HDL event controls, and most testbench features remain work in progress.
Unsupported syntax is diagnosed rather than silently accepted.

## Requirements

- Windows or Linux on x86-64
- A C++20 compiler: GCC or Clang on Linux, or MSVC on Windows
- CMake 3.28 or newer
- LLVM **22.1.8** for the supported compiled-code configuration
- Boost.Context **1.91.0** for executable SystemC threads
- Tcl **9.0.4 or newer in the 9.0 release series**, or network access for
  CMake's pinned fallback

LLVM is isolated behind one adapter. Frontend, interpreter, and most unit tests
can be developed without LLVM by configuring `FSIM_LLVM_MODE=OFF`. The
checked-in CMake configuration accepts only LLVM 22.1.8 when the backend is
enabled. The checked-in Linux and Windows LLVM CI jobs configure and run the
adapter tests against that exact version, including its C runtime-table header
test and O0/O2 ORC tests. Linux GCC and Windows MSVC are also exercised without
LLVM in Debug and Release configurations; separate Linux jobs run the suite
with ASan/UBSan and exercise the VHDL parser plus both Verilog/SV
preprocessor/parser entry points with Clang/libFuzzer. Adapter developers may
manually smoke-test an older LLVM while
bootstrapping, but that is not a supported project configuration and must not
be used to claim v1 compatibility.

The planned release dependency pins are CLI11 2.6.2, toml++ 3.4.0,
Boost.Context 1.91.0, and Catch2 3.15.2. CMake first uses an installed exact
Boost.Context 1.91.0 package and otherwise fetches Boost's official pinned
source archive with SHA-256 verification. Set
`FSIM_SYSTEMC_FIBER_MODE=OFF` only for a dependency-free build that
intentionally diagnoses `SC_THREAD`/`SC_CTHREAD` as non-executable.

CMake accepts an installed Tcl 9.0 development package at patchlevel 9.0.4 or
newer. An older or different Tcl release is ignored and CMake downloads the
pinned Tcl 9.0.4 source archive, verifies its SHA-256 digest, builds the static
core with Tcl's native Linux or MSVC build, and installs the Tcl
standard-library scripts in a relocatable fsim data directory. Set
`FSIM_TCL_LIBRARY` to an alternate standard-library directory when packaging
with a custom layout. Set `FSIM_TCL_MODE=OFF` only when intentionally building
without the Tcl command.

The first Tcl slice supports `fsim tcl` for a multiline interactive shell,
`fsim tcl -c SCRIPT` for repeatable batch commands, and
`fsim tcl FILE [ARG ...]` for scripts with standard Tcl argument variables.
It exposes `fsim::version`, project metadata, check/build, signal enumeration
and reads, deposit/force/release, run, and status commands over the same native
application model. The stateful `fsim::debug COMMAND ?ARG ...?` adapter also
exposes scope/signal/local inspection, debugger mutation, relative and absolute
runs, source/time/conditional-signal breakpoints, and
statement/process/delta/time stepping through the same O0 debugger engine used
by the CLI. `fsim::diagnostics`, `fsim::stop`, and `fsim::trace` provide
structured diagnostic lifecycle, callback-safe stop requests, and live
add/remove/all/clear/list VCD selection. `fsim::on`, `fsim::off`, and
`fsim::callbacks` register synchronous safe-point, value-change, and lifecycle
command-prefix callbacks; callback failures stop the run and become catchable
Tcl errors. Safe-point observers compose with debugger and interrupt control,
so callback-driven stops also work during `fsim::debug` runs. Assertion
callbacks receive process, severity, message, and source metadata while adding
a structured diagnostic. `fsim::project load` transactionally replaces the
manifest and resets any live, finished, or poisoned session, while
`fsim::trace configure|disable|status` controls the next debugger trace before
simulation starts. Interactive and batch Python support is planned later,
after the Tcl and native control contracts stabilize.

The native C API accepts append-only structure prefixes and never reads or
writes fields beyond the caller-advertised size. Its hierarchy includes
generation-checked signal, port, process, and packed procedural-variable
objects with source metadata where a declaration location is available.
Nested procedural blocks are explicit lexical-scope objects, and metadata
distinguishes never-entered scopes and uninitialized locals.
Elaborated child specializations are explicit instance scopes whose direct
signal/process children retain generation-safe hierarchy handles.
Conditional and iterative generate regions are retained as nested scopes,
including indexed region names and generated local behavior.
Supported process outputs are exposed as source-bearing driver children of
their signals, with current-value reads in the single-driver runtime slice.
An append-only detailed safe-point callback supplies scheduler phases and
source-bearing executable point kinds while preserving the original v1
callback prefix.

## Build and test

On Linux:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

To point CMake at the supported LLVM installation:

```sh
cmake --preset dev -DFSIM_LLVM_MODE=ON \
  -DLLVM_DIR=/path/to/llvm-22.1.8/lib/cmake/llvm
cmake --build --preset dev
```

An equivalent configuration without a preset is:

```sh
cmake -S . -B build/llvm -DCMAKE_BUILD_TYPE=Debug \
  -DFSIM_LLVM_MODE=ON \
  -DLLVM_DIR=/path/to/llvm-22.1.8/lib/cmake/llvm
cmake --build build/llvm
ctest --test-dir build/llvm --output-on-failure
```

On Windows, install LLVM 22.1.8 for x86-64 and use the checked-in preset:

```powershell
cmake --preset windows-msvc -DLLVM_DIR=C:\llvm-22.1.8\lib\cmake\llvm
cmake --build --preset windows-msvc
ctest --test-dir build\windows-msvc -C Release --output-on-failure
```

The `ci-sanitizers` preset runs the non-LLVM suite locally with GCC ASan/UBSan
at each scheduled ten-batch boundary; the hosted CI workflow excludes
sanitizer instrumentation. The `ci-fuzz` preset additionally requires Clang
and its libFuzzer/compiler-rt development package; it recompiles an isolated
coverage-instrumented copy of the frontend without ASan or UBSan, so fuzzers
and ordinary tests may be enabled in the same build without adding a
libFuzzer entry point to normal executables.

## Command-line use

The primary interface is:

```text
fsim check
fsim build
fsim run
fsim debug
fsim tcl
fsim compile
fsim systemc compile
fsim systemc link
fsim elaborate
fsim simulate
```

For example:

```sh
build/dev/fsim check -p examples/vertical_slice/fsim.toml
build/dev/fsim build -p examples/vertical_slice/fsim.toml
build/dev/fsim run   -p examples/vertical_slice/fsim.toml
build/dev/fsim debug -p examples/vertical_slice/fsim.toml
build/dev/fsim tcl -c 'puts [fsim::version]'
```

Direct source files are also accepted:

```sh
build/dev/fsim check --lang systemverilog examples/vertical_slice/tb.sv
```

Manifest-free, restartable artifact phases are available for portable HDL:

```sh
fsim compile --lang systemverilog --standard 2017 --library work \
  --output unit.fsimobj source.sv
fsim elaborate --object unit.fsimobj --top top=sv:work.top \
  --output design.fsimdesign
fsim simulate --design design.fsimdesign --engine compiled \
  --cache .fsim-native --file-root . --trace run.vcd
```

SystemC translation units and their linked logical-library plug-in are equally
explicit:

```sh
fsim systemc compile --output bridge.fsimscobj bridge.cpp
fsim systemc link --object bridge.fsimscobj --library models \
  --output models.fsimscplugin
fsim elaborate --object unit.fsimobj \
  --systemc-plugin models.fsimscplugin --top top=sv:work.top \
  --search-library models --output design.fsimdesign
```

Each SystemC translation unit has its own dependency-complete compile cache;
linking records the ordered object identities and exported factory schemas.
Artifact directories are immutable and overwrite-safe. A design that selects
a SystemC factory embeds the checksummed native plug-in and reconstructs its
hierarchy during standalone simulation, so producer sources and intermediate
objects are no longer needed. See the
[non-project phase tutorial](examples/non_project_phases/README.md).

For Verilog/SystemVerilog source sets, `compilation_unit = "file"` resets
macro and directive context for every listed file, `"source-set"` shares
ordered context across that source set, and `"combined"` shares context across
all combined source sets with the same language and standard. Combined sets
retain their declared libraries; their include directories and manifest
definitions are accumulated in source-set order. VHDL files remain independent
analysis units.

`--diagnostics=json` selects structured diagnostics. Manifest values can be
overridden with options such as `--top`, `--duration`, `--max-deltas`,
`--delay-mode`, `--trace`, `--seed`, `-O`, and `-j`. Parenthesized Verilog or
SystemVerilog `min:typ:max` delays use schema-1
`[run].delay_mode = "min" | "typ" | "max"`; `typ` is the deterministic
default and `--delay-mode` overrides it. Continuous assignments accept one,
two, or three delay values for rise, fall, and turnoff; each may be a triplet.
Supported gates accept one or two. Selection precedes precision rounding and
automatic resolution, and delayed continuous writes reject superseded pulses
inertially while procedural delayed NBA remains transport.
Blocking and nonblocking procedural assignments also accept bounded
intra-assignment delay or event controls. Blocking delays capture before
suspending, event controls evaluate after waking, and NBAs publish in
deterministic source/stable-process order with last-assignment behavior for
overlapping whole and packed-slice targets.
One-element VHDL signal waveforms accept implicit or explicit inertial,
transport, and optional `reject TIME inertial`. Rejection limits and waveform
delays normalize exactly to project ticks; the runtime edits projected
transactions independently for every packed scalar subelement.

Schema 2 also accepts an ordered set of aliased simulation roots. Every root
shares one scheduler, time domain, library/package index, trace, debugger, and
native session, but retains its own instance-local state:

```toml
[project]
name = "device-with-global-signals"
time_resolution = "1ns"

[[project.top]]
target = "sv:work.device_tb"
alias = "dut"

[[project.top]]
target = "sv:vendor.glbl"
alias = "glbl"
```

The equivalent command-line replacement is `--top dut=device_tb --top
glbl=vendor.glbl`. When more than one `--top` is present, every occurrence
must have a unique portable alias. A single legacy `[project].top` or one
unaliased `--top NAME` remains supported. SystemVerilog roots may read or drive
another root's packed root-level signal through its ordinary top-level
hierarchical name, such as `glbl.GSR`; deeper descendant shortcuts are
rejected and should be surfaced through a root port or signal. Trace filters,
debugger paths, Tcl hierarchy values, and C API lookup use the selected aliases
as their first path component.

Random facilities use deterministic per-process streams and default to project
seed `1`. A numeric `seed`/`--seed` value reproduces a run. Explicit
`--seed=random` selects host entropy once and prints the effective numeric seed
so it can be reused.

The current debugger supports relative or absolute time runs,
statement/process/delta/time stepping, source/time/signal-change breakpoints,
scope/signal navigation, value inspection, and deposit/force/release. With LLVM
enabled, `fsim debug` forces O0 compilation for eligible processes and retains
per-process interpreter fallback; builds without LLVM use the interpreter.
SimIR retains source-bearing statement, call, wait, assertion, process-entry,
and process-suspension points. O0 generated code always exposes those points,
while O2 tests one size-gated runtime flag so ordinary runs continue through
them.
Signal breakpoints accept exact-state `==`/`!=` conditions. When a debug VCD is
configured, `trace add`, `trace remove`, `trace all`, `trace clear`, and
`trace list` change the live committed-value selection. Bounded packed process
variables in nested SystemVerilog lexical blocks retain shadowing and
block-entry initialization semantics and are shown with stable hierarchical
names by `locals` through interpreter or compiled frames. A design
`$finish` is terminal for that simulation; a debugger or Ctrl-C stop remains
resumable, while a fatal runtime error poisons the simulation and prevents
further execution. The CLI installs its SIGINT handler only for the active
run/debug command and restores the host's previous handler on exit.

The native C session API also exposes tested statement/process/delta/time
stepping and an asynchronous stop request that may be issued from a synchronous
safe-point callback. Executable safe-point callbacks carry the current process
handle. Debug-visible process variables are generation-safe child objects with
full-path lookup, typed metadata, and canonical retained-value reads; locals in
blocks that have never executed report unavailable in both interpreter and
compiled modes. A terminal HDL stop takes precedence when it coincides with an
external step/stop request, so a finished design is never reported resumable.

With LLVM enabled, `fsim build` compiles eligible processes and `fsim run`
uses a hybrid engine. Native word operations execute through LLVM at the
selected O0/O2 setting—O2 by default—while typed capability misses fall back
per process to the reference evaluator under the same deterministic kernel.
Validated service operations can own arbitrary-width values without narrowing:
the binary `$fread`/packed-memory path has exact 137-bit, fully compiled O0/O2
cold/warm evidence. Generated callbacks and the reference kernel share checked
allocation-free single-word `Logic4` and four-plane `Logic9` representations
for values up to 64 bits, while the owning runtime/API/artifact paths use
word-vector `PackedLogic4` storage for wider Logic4 values. Generated
Logic9 code preserves all nine ordinal states through constants, reads,
structural operations, IEEE logical operators, exact equality, formatting,
debug frames, and blocking, update-phase, delayed, inertial, projected,
partial, and atomic waveform writes. Explicit register/signal domain
conversion prevents a Logic9 value from silently entering the aval/bval path.
The plain-C runtime-table ABI retains its v1 prefix and appends pointer-based
Logic9 callbacks and caller-owned frame planes so aggregate calling
conventions never cross the compiler boundary; generated code size-gates
those fields per process before use. The configured
cache stores one native object per compiled specialization module under
`llvm-native`; `fsim build` reports compiled process/module counts and native
cache hits, misses, stores, rejected entries, and maintenance failures. The
LLVM adapter performs best-effort startup pruning with defaults of 10 GiB,
10,000 entries, and 30 days; successful hits refresh entry recency, live
per-key locks are never stolen, and stale publisher temporaries are removed
after a one-hour grace period. LLVM O0/O2 object identity
includes the specialization-module identity and ordered process keys. Each
module identity includes a provenance key for the exact owning-root and
ordered Verilog/SystemVerilog transitive-include bytes supplied to the
preprocessor/parser, source path, language standard, library, compilation-unit
mode, macro/include settings, separated VHDL entity-interface source,
bundled-library version marker, and represented generic/parameter values. Each
process key includes
scheduled-write kind and the exact delayed-write delay, plus wait kind,
ordered operands, widths, dynamic edges, and static sensitivity signal/edge
data. An unrelated,
uninstantiated source edit therefore retains the specialization's native
object, while even a comment-only edit to its owning source invalidates it.
`WaitOn`, `WaitSensitivity`, and `WaitForever` use appended resume-status
values while keeping the v1 result layout and its existing status values
unchanged. The result identifies the boundary instruction; immutable SimIR
retains the dynamic signal/edge list and static edge rules for the kernel.
Consequently,
any-change sensitivity-only signals may exceed 64 bits because no signal value
crosses the generated ABI; edge-qualified sensitivities require scalar
signals. Builds without LLVM execute entirely through the reference evaluator.
The bounded O0 debug path is differentially tested against the interpreter for
source breakpoints and statement/process/scheduler stepping.
Direct allocation-free native arithmetic remains a single-word optimization;
eligible arbitrary-width operations execute through exact caller-owned frame
planes and validated runtime services, with deterministic per-process fallback
only for an unsupported capability. SystemVerilog's Batch 151 and Verilog-2005
Batch 164 surfaces preserve arbitrary-width constants, literals, aggregates,
callables, mixed boundaries, debugger/trace/API/file services, artifacts, and
native-cache identity under explicit work/storage budgets.

The application suite also compares a bounded scheduled-write design exactly
between the interpreter and O2 hybrid engine. It checks an update commit at
tick 0, a delayed commit at tick 2, and the final value and scheduler
observations. The bounded differential harness also requires byte-identical
normalized VCD output, including declarations, initial values, timestamps, and
committed changes, across interpreter and O0/O2 hybrid execution. A separate
interpreter/compiled case schedules past
`UINT64_MAX`, verifies that the original scheduler overflow exception is
contained across the generated callback boundary and rethrown, and confirms
that the simulation is poisoned without publishing the write.

An additional exact application comparison runs the bounded
`always @(posedge trigger)` path through the interpreter and O2 hybrid engine.
Both processes compile in the hybrid run. The expected changes are the initial
trigger value at tick 0, the trigger edge at tick 1/delta 0, the observed
update at tick 1/delta 1, and the falling trigger at tick 2/delta 0. This is
evidence for the represented positive-edge form, not complete Verilog or
SystemVerilog event-control semantics.

The [vertical-slice example](examples/vertical_slice/README.md) is intentionally
small. It runs an SV testbench containing an explicitly bound VHDL counter and
an SV combinational child. The final committed values are `counter_q = 1` and
`child_y = FE`; those committed top-level signals are recorded in its VCD. The
LLVM-enabled application test also runs this bounded mixed hierarchy through
the reference and O2 hybrid engines and compares status, time, delta,
committed-change callbacks, and final values. Assertion metadata, normalized
VCD comparison, and bidirectional bounded VHDL/SystemVerilog
construction-actual/cache tests are also automated. Exhaustive semantic
fixtures and Windows execution evidence remain open.

The [three-language hierarchy tutorial](examples/three_language_hierarchy/README.md)
builds on that slice with a SystemVerilog top, a SystemC factory and method,
and a VHDL child in one recursive hierarchy. It includes executable check,
build, run, VCD/FST, hierarchy-navigation, breakpoint, and trace-selection
steps. VCD remains the default; the sibling FST manifest records the same
canonical mixed-language declarations and events with deterministic portable
compression.

## Design documents

- [Architecture](docs/architecture.md)
- [v1 resume handoff](docs/v1-resume.md)
- [Implementation plan and progress](docs/implementation-plan.md)
- [Deterministic cross-language semantics](docs/cross-language-semantics.md)
- [Diagnostic code catalog](docs/diagnostics.md)
- [VCD and FST tracing](docs/tracing.md)
- [Public tracing API](docs/api.md)
- [Batch 171 FST release audit](docs/v2-fst-release-audit.md)
- [Standard Delay Format support](docs/sdf.md)
- [Batch 169 SDF application release audit](docs/v2-sdf-application-release-audit.md)
- [Batch 170 SDF/VITAL release audit](docs/v2-sdf-vital-release-audit.md)
- [Language support and feature status](docs/language-support.md)
- [Feature matrix and test evidence](docs/feature-matrix.md)
- [v1 conformance provenance audit](docs/v1-conformance-audit.md)
- [SystemC subset and plug-in model](docs/systemc-subset.md)
- [SystemVerilog UVM foundation](docs/systemverilog-uvm.md)
- [Producer-independent UVM tutorial](docs/uvm-tutorial.md)
- [Governed external UVM source provenance](docs/uvm-source-provenance.md)
- [UVM release-closure audit](docs/uvm-closure-audit.md)

## Licensing

fsim source code is licensed under the
[Apache License 2.0](LICENSE). Files use SPDX identifier
`Apache-2.0`. Third-party sources and dependencies retain their own licenses
and must pass license review before they are bundled. The official IEEE-P1076
`1076-2019` VHDL package source snapshot is bundled byte-for-byte under its
Apache-2.0 license with pinned provenance and checksums; the
[feature matrix](docs/feature-matrix.md) identifies which reviewed package
profiles are currently executable. The
[v1 conformance audit](docs/v1-conformance-audit.md) pins every public
behavioral reference, records excluded license classes, and explicitly forbids
unreviewed test-text imports.
