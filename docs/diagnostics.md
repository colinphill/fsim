<!-- SPDX-License-Identifier: Apache-2.0 -->
# Diagnostic code catalog

This catalog describes every literal diagnostic code emitted by the current
production sources. It documents the internal vertical slice, not the eventual
complete v1 implementation. A code identifies a diagnostic class; paths,
source locations, messages, and notes provide the instance-specific detail.
Code spellings are the stable, machine-readable part of the current
diagnostic interface; message wording may evolve.

The severity column is the severity assigned by the current emitter. All
entries are errors unless explicitly marked as warnings. The three strings
`FSIM-OBJECT-CACHE-V1`, `FSIM-CACHE-LOCK-V1`, and
`FSIM-DESIGN-CACHE-V1` are persistent-format markers, not diagnostics, and are
therefore excluded.

## Command line and C API

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-CLI-0001` | error | Invalid command-line argument, option, value, command, or direct-source combination. |
| `FSIM-CLI-0002` | error | The selected command has no connected implementation in this build. |
| `FSIM-CLI-0003` | error | An exception escaped command dispatch. |
| `FSIM-API-0002` | error | A C API check or build was requested before loading a project. |
| `FSIM-API-0003` | error | A C API operation requires a successfully built design. |
| `FSIM-API-ASSERT-0001` | assertion severity | A false HDL assertion stopped simulation through the C API; the diagnostic carries its process, source location, severity, and message. |
| `FSIM-API-RUN-0001` | error | Simulation invoked through the C API failed at runtime. |
| `FSIM-API-VALUE-0001` | error | A C API deposit or force value is invalid for the selected signal. |

## Project manifest

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-PROJ-0001` | error | TOML syntax or lexical error in the project manifest. |
| `FSIM-PROJ-0002` | error | A manifest value has the wrong type. |
| `FSIM-PROJ-0003` | error | Unknown manifest table or key. |
| `FSIM-PROJ-0004` | error | Duplicate table declaration or key assignment. |
| `FSIM-PROJ-0005` | error | Missing, unsupported, or out-of-range schema version. |
| `FSIM-PROJ-0006` | error | Required project, source-set, or binding field is missing. |
| `FSIM-PROJ-0007` | error | A manifest value is outside its accepted range or vocabulary. |
| `FSIM-PROJ-0008` | error | Project-manifest or source-glob I/O failed. |
| `FSIM-PROJ-0009` | error | A listed source or source-glob root is missing, or a glob matched nothing. |

## Common frontend and source analysis

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-FE-0001` | error | The project contains no HDL or SystemC source files. |
| `FSIM-FE-0002` | error | More than one parsed source defines the same design-unit identity. |
| `FSIM-FE-0003` | error | A parallel source-analysis task failed or produced no result. |
| `FSIM-FE-CU-0001` | warning | Requested source-set compilation-unit grouping is preserved but not implemented; files are parsed independently. |
| `FSIM-FE-IO-001` | error | An HDL source file could not be opened. |
| `FSIM-FE-IO-002` | error | Reading an HDL source file failed after it was opened. |
| `FSIM-FE-IO-003` | error | HDL language inference failed for the source-file extension. |
| `FSIM-FE-LEX-001` | error | The lexer encountered an unexpected character. |
| `FSIM-FE-LEX-002` | error | A Verilog/SystemVerilog block comment is unterminated. |
| `FSIM-FE-LEX-003` | error | An extended identifier is unterminated. |
| `FSIM-FE-LEX-004` | error | A string literal is unterminated. |
| `FSIM-FE-PARSE-001` | error | A parser expectation using the common fallback code failed. |
| `FSIM-FE-PP-0001` | error | HDL include directories or macro definitions require the unimplemented HDL preprocessor. |

## VHDL frontend

### VHDL syntax

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-VHDL-PARSE-001` | error | Expected a VHDL identifier, such as a unit, object, type, or selected name. |
| `FSIM-VHDL-PARSE-002` | error | Expected `is` after an entity name. |
| `FSIM-VHDL-PARSE-003` | error | Expected `(` after `port`. |
| `FSIM-VHDL-PARSE-004` | error | Expected `:` after a port name. |
| `FSIM-VHDL-PARSE-005` | error | Expected a VHDL port mode. |
| `FSIM-VHDL-PARSE-006` | error | Expected `;` between port declarations. |
| `FSIM-VHDL-PARSE-007` | error | Expected `)` after port declarations. |
| `FSIM-VHDL-PARSE-008` | error | Expected `;` after a port clause. |
| `FSIM-VHDL-PARSE-009` | error | Expected `to` or `downto` in the supported locally static range form. |
| `FSIM-VHDL-PARSE-010` | error | Expected `)` after a VHDL range. |
| `FSIM-VHDL-PARSE-011` | error | Expected an integer literal in a signed decimal bound. |
| `FSIM-VHDL-PARSE-012` | error | Expected `end` in a design-unit end clause. |
| `FSIM-VHDL-PARSE-013` | error | Expected `;` after a design-unit end clause. |
| `FSIM-VHDL-PARSE-014` | error | Expected `of` in an architecture header. |
| `FSIM-VHDL-PARSE-015` | error | Expected `is` in an architecture header. |
| `FSIM-VHDL-PARSE-016` | error | Expected `begin` before architecture statements. |
| `FSIM-VHDL-PARSE-017` | error | Expected `:` after a signal name. |
| `FSIM-VHDL-PARSE-018` | error | Expected `;` after a signal declaration. |
| `FSIM-VHDL-PARSE-019` | error | Expected `process` at a process statement. |
| `FSIM-VHDL-PARSE-020` | error | Expected `)` after a process sensitivity list. |
| `FSIM-VHDL-PARSE-021` | error | Expected `begin` before process statements. |
| `FSIM-VHDL-PARSE-022` | error | Expected `end` after process statements. |
| `FSIM-VHDL-PARSE-023` | error | Expected `;` after a process statement. |
| `FSIM-VHDL-PARSE-024` | error | Expected `end` after an `if` statement. |
| `FSIM-VHDL-PARSE-025` | error | Expected `if` after `end`. |
| `FSIM-VHDL-PARSE-026` | error | Expected `;` after an `if` statement. |
| `FSIM-VHDL-PARSE-027` | error | Expected `;` after `null`. |
| `FSIM-VHDL-PARSE-028` | error | Expected `then` after an `if` condition. |
| `FSIM-VHDL-PARSE-029` | error | Expected `;` after an assignment. |
| `FSIM-VHDL-PARSE-030` | error | Expected a delay magnitude after `after`. |
| `FSIM-VHDL-PARSE-031` | error | Expected `)` after a slice. |
| `FSIM-VHDL-PARSE-032` | error | Expected `)` after an index. |
| `FSIM-VHDL-PARSE-033` | error | Expected `)` after call arguments. |
| `FSIM-VHDL-PARSE-034` | error | Expected `)` after a parenthesized expression. |
| `FSIM-VHDL-PARSE-035` | error | Expected a VHDL expression. |
| `FSIM-VHDL-PARSE-036` | error | Expected `)` after an architecture name in an entity aspect. |
| `FSIM-VHDL-PARSE-037` | error | Expected `map` after `generic`. |
| `FSIM-VHDL-PARSE-038` | error | Expected `(` after `generic map`. |
| `FSIM-VHDL-PARSE-039` | error | Expected `port map` in an instance. |
| `FSIM-VHDL-PARSE-040` | error | Expected `map` after `port`. |
| `FSIM-VHDL-PARSE-041` | error | Expected `(` after `port map`. |
| `FSIM-VHDL-PARSE-042` | error | Expected `)` after port associations. |
| `FSIM-VHDL-PARSE-043` | error | Expected `;` after a VHDL instance. |
| `FSIM-VHDL-PARSE-044` | error | A VHDL library, use, or context-reference clause is malformed or unterminated. |
| `FSIM-VHDL-PARSE-045` | error | Expected a string literal after an assertion `report`. |
| `FSIM-VHDL-PARSE-046` | error | Expected `;` after an assertion. |
| `FSIM-VHDL-PARSE-047` | error | Expected `:` after process-variable names. |
| `FSIM-VHDL-PARSE-048` | error | Expected `;` after a process-variable declaration. |
| `FSIM-VHDL-PARSE-049` | error | Expected `;` after a wait statement. |

### VHDL semantics and bounded-subset rejections

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-VHDL-SEM-001` | error | A packed range bound is not a supported decimal integer literal. |
| `FSIM-VHDL-SEM-002` | error | Duplicate port declaration. |
| `FSIM-VHDL-SEM-003` | error | Duplicate signal declaration. |
| `FSIM-VHDL-SEM-004` | error | A delay magnitude is not an integer literal. |
| `FSIM-VHDL-SEM-011` | error | An assertion severity is not `note`, `warning`, `error`, or `failure`. |
| `FSIM-VHDL-SEM-012` | error | A process combines a sensitivity list with an explicit wait statement. |
| `FSIM-VHDL-UNSUPPORTED-001` | error | Unsupported design unit or context item. |
| `FSIM-VHDL-UNSUPPORTED-002` | error | Generic clauses are not implemented in this frontend slice. |
| `FSIM-VHDL-UNSUPPORTED-003` | error | Unsupported entity declaration. |
| `FSIM-VHDL-UNSUPPORTED-004` | error | Unsupported architecture declaration. |
| `FSIM-VHDL-UNSUPPORTED-005` | error | Unsupported labeled concurrent statement. |
| `FSIM-VHDL-UNSUPPORTED-006` | error | Unsupported concurrent statement. |
| `FSIM-VHDL-UNSUPPORTED-007` | error | A process declarative item is not a bounded variable declaration. |
| `FSIM-VHDL-UNSUPPORTED-008` | error | Unsupported sequential statement. |
| `FSIM-VHDL-UNSUPPORTED-009` | error | Generic maps are not implemented. |
| `FSIM-VHDL-UNSUPPORTED-010` | error | A port-map actual is not a simple identifier. |
| `FSIM-VHDL-UNSUPPORTED-011` | error | Port default expressions are parsed but not executable. |
| `FSIM-VHDL-UNSUPPORTED-012` | error | Signal initializers are parsed but not executable. |
| `FSIM-VHDL-UNSUPPORTED-013` | error | A subtype requires semantic type resolution not implemented in this slice. |
| `FSIM-VHDL-UNSUPPORTED-014` | error | An integer-family object is parsed but not executable. |
| `FSIM-VHDL-UNSUPPORTED-015` | error | VHDL context declarations are not implemented in this frontend slice. |
| `FSIM-VHDL-UNSUPPORTED-016` | error | A wait form requires unsupported bare, `until`, or combined-clause semantics. |
| `FSIM-VHDL-UNSUPPORTED-017` | error | A wait is nested in conditional control flow requiring suspension-path analysis. |

## Verilog and SystemVerilog frontend

### Verilog/SystemVerilog syntax

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-SV-PARSE-001` | error | Expected a Verilog/SystemVerilog identifier. |
| `FSIM-SV-PARSE-002` | error | Expected `)` after module ports. |
| `FSIM-SV-PARSE-003` | error | Expected `;` after a module header. |
| `FSIM-SV-PARSE-004` | error | Expected `endmodule`. |
| `FSIM-SV-PARSE-005` | error | Expected `:` in a packed range. |
| `FSIM-SV-PARSE-006` | error | Expected `]` after a packed range. |
| `FSIM-SV-PARSE-007` | error | Expected an integer literal in a signed decimal bound. |
| `FSIM-SV-PARSE-008` | error | Expected `;` after a declaration. |
| `FSIM-SV-PARSE-009` | error | Expected a continuous-assignment target. |
| `FSIM-SV-PARSE-010` | error | Expected `=` in a continuous assignment. |
| `FSIM-SV-PARSE-011` | error | Expected `;` after a continuous assignment. |
| `FSIM-SV-PARSE-012` | error | An `always` process lacks the required event control for this slice. |
| `FSIM-SV-PARSE-013` | error | Expected `initial`. |
| `FSIM-SV-PARSE-014` | error | Expected `(` after `@`. |
| `FSIM-SV-PARSE-015` | error | Expected `)` after `@*`. |
| `FSIM-SV-PARSE-016` | error | Expected `)` after a sensitivity list. |
| `FSIM-SV-PARSE-017` | error | Expected `end` after a procedural block. |
| `FSIM-SV-PARSE-018` | error | Expected `(` after `if`. |
| `FSIM-SV-PARSE-019` | error | Expected `)` after an `if` condition. |
| `FSIM-SV-PARSE-020` | error | Expected `)` after `$finish` arguments. |
| `FSIM-SV-PARSE-021` | error | Expected `;` after `$finish`. |
| `FSIM-SV-PARSE-022` | error | Expected `;` after a procedural assignment. |
| `FSIM-SV-PARSE-023` | error | Expected a delay magnitude. |
| `FSIM-SV-PARSE-024` | error | Expected `)` after a parenthesized delay. |
| `FSIM-SV-PARSE-025` | error | Expected `]` after an assignment-target part-select. |
| `FSIM-SV-PARSE-026` | error | Expected `]` after an assignment-target index. |
| `FSIM-SV-PARSE-027` | error | Expected `:` in a conditional expression. |
| `FSIM-SV-PARSE-028` | error | Expected `)` after call arguments. |
| `FSIM-SV-PARSE-029` | error | Expected `)` after a parenthesized expression. |
| `FSIM-SV-PARSE-030` | error | Expected `}` after a concatenation. |
| `FSIM-SV-PARSE-031` | error | Expected an expression. |
| `FSIM-SV-PARSE-032` | error | Expected `]` after an expression part-select. |
| `FSIM-SV-PARSE-033` | error | Expected `]` after an expression index. |
| `FSIM-SV-PARSE-034` | error | Expected `(` after an instance name. |
| `FSIM-SV-PARSE-035` | error | Expected `(` after a named port. |
| `FSIM-SV-PARSE-036` | error | Expected `)` after a named port connection. |
| `FSIM-SV-PARSE-037` | error | Expected `)` after instance connections. |
| `FSIM-SV-PARSE-038` | error | Expected `;` after a module instance. |
| `FSIM-SV-PARSE-039` | error | Expected `(` after `assert`. |
| `FSIM-SV-PARSE-040` | error | Expected `)` after an assertion condition. |
| `FSIM-SV-PARSE-041` | error | Expected `$error` after an assertion `else`. |
| `FSIM-SV-PARSE-042` | error | Expected a string literal argument to `$error`. |
| `FSIM-SV-PARSE-043` | error | Expected `)` after an assertion `$error` message. |
| `FSIM-SV-PARSE-044` | error | Expected `;` after an assertion. |
| `FSIM-SV-PARSE-045` | error | Expected `;` after a procedural variable declaration. |
| `FSIM-SV-PARSE-046` | error | Expected `(` after `case`. |
| `FSIM-SV-PARSE-047` | error | Expected `)` after a `case` selector expression. |
| `FSIM-SV-PARSE-048` | error | Expected `:` after a `case` item. |
| `FSIM-SV-PARSE-049` | error | Expected `endcase`. |
| `FSIM-SV-PARSE-039` | error | Expected a time-unit magnitude after `` `timescale``. |
| `FSIM-SV-PARSE-040` | error | Expected a time-unit name after the `` `timescale`` magnitude. |
| `FSIM-SV-PARSE-041` | error | Expected `/` between `` `timescale`` unit and precision. |
| `FSIM-SV-PARSE-042` | error | Expected a time-precision magnitude after `/`. |
| `FSIM-SV-PARSE-043` | error | Expected a time-precision unit. |

### Verilog/SystemVerilog semantics and bounded-subset rejections

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-VERILOG-SEM-001` | error | `always_ff` was used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-VERILOG-SEM-002` | error | `always_comb` was used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-VERILOG-SEM-003` | error | `always_latch` was used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-SV-SEM-001` | error | A packed range bound is not a supported decimal integer literal. |
| `FSIM-SV-SEM-002` | error | A delay magnitude is not a decimal integer literal. |
| `FSIM-SV-SEM-003` | error | Duplicate module-port declaration in the module header. |
| `FSIM-SV-SEM-004` | error | Duplicate non-ANSI body port declaration. |
| `FSIM-SV-SEM-005` | error | Duplicate declaration or type refinement of a non-ANSI port. |
| `FSIM-SV-SEM-006` | error | Duplicate internal signal declaration. |
| `FSIM-SV-SEM-007` | error | Invalid `` `timescale`` magnitude or time unit. |
| `FSIM-SV-SEM-008` | error | A `` `timescale`` value exceeds fsim's 64-bit time range. |
| `FSIM-SV-SEM-009` | error | `` `timescale`` precision is coarser than its time unit. |
| `FSIM-SV-SEM-010` | error | A delay overflows after applying the module `` `timescale``. |
| `FSIM-SV-SEM-011` | error | An `always_comb` or `always_latch` has an explicit event control. |
| `FSIM-SV-SEM-012` | error | An `always_comb` or `always_latch` contains a timing control. |
| `FSIM-SV-SEM-013` | error | An `always_comb` or `always_latch` contains a nonblocking assignment. |
| `FSIM-SV-SEM-014` | error | A `case` statement contains more than one `default` item. |
| `FSIM-SV-UNSUPPORTED-001` | error | Unsupported compilation-unit item. |
| `FSIM-SV-UNSUPPORTED-002` | error | A directive requires the unimplemented preprocessing stage. |
| `FSIM-SV-UNSUPPORTED-003` | error | Parameter port lists are not implemented. |
| `FSIM-SV-UNSUPPORTED-004` | error | Unsupported module item. |
| `FSIM-SV-UNSUPPORTED-005` | error | A named port connection was used where a module-header declaration is required. |
| `FSIM-SV-UNSUPPORTED-006` | error | Unpacked port dimensions are not implemented. |
| `FSIM-SV-UNSUPPORTED-007` | error | Unpacked arrays are not implemented. |
| `FSIM-SV-UNSUPPORTED-008` | error | Unsupported identifier-starting procedural statement. |
| `FSIM-SV-UNSUPPORTED-009` | error | Unsupported procedural statement. |
| `FSIM-SV-UNSUPPORTED-010` | error | ANSI port default expressions are parsed but not executable. |
| `FSIM-SV-UNSUPPORTED-011` | error | Declaration initializers are parsed but not executable. |
| `FSIM-SV-UNSUPPORTED-012` | error | Integer objects are parsed but not executable. |
| `FSIM-SV-UNSUPPORTED-013` | error | `` `default_nettype`` is recognized but implicit-net legality is not implemented. |
| `FSIM-SV-UNSUPPORTED-014` | error | A procedural declaration uses the net-only `wire` type. |
| `FSIM-SV-UNSUPPORTED-015` | error | A nested procedural block declaration requires unsupported local-scope semantics. |
| `FSIM-SV-UNSUPPORTED-016` | error | `casez` or `casex` requires unsupported wildcard matching. |
| `FSIM-SV-UNSUPPORTED-017` | error | A `case` statement uses an unsupported `unique`, `unique0`, or `priority` qualifier. |
| `FSIM-SV-UNSUPPORTED-018` | error | A `case inside` statement requires unsupported set-membership matching. |

## Elaboration and SimIR lowering

### Top selection, objects, and executable lowering

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-ELAB-0001` | error | No unique executable top can be inferred; set `project.top` or `--top`. |
| `FSIM-ELAB-001` | error | The requested top-level design unit was not found. |
| `FSIM-ELAB-002` | error | A VHDL architecture has no matching entity. |
| `FSIM-ELAB-003` | error | A qualified top-level target is malformed. |
| `FSIM-ELAB-004` | error | A qualified VHDL top does not name an architecture. |
| `FSIM-ELAB-010` | error | A signal has an invalid packed width. |
| `FSIM-ELAB-011` | error | The design exceeds the dense 32-bit signal-ID limit. |
| `FSIM-ELAB-020` | error | A process sensitivity names an unknown signal. |
| `FSIM-ELAB-030` | error | A parsed delay statement contains no delay value. |
| `FSIM-ELAB-031` | error | An assignment target is not a supported packed object, constant bit-select, or constant part-select. |
| `FSIM-ELAB-032` | error | An assignment target names an unknown signal or local variable. |
| `FSIM-ELAB-040` | error | An expression names an unknown identifier. |
| `FSIM-ELAB-041` | error | A literal is malformed or unsupported by executable lowering. |
| `FSIM-ELAB-042` | error | An operator was parsed but has no executable SimIR lowering. |
| `FSIM-ELAB-043` | error | An expression form was parsed but has no executable SimIR lowering. |
| `FSIM-ELAB-045` | error | A VHDL edge predicate appears outside the one supported process-guard form. |
| `FSIM-ELAB-046` | error | A procedural blocking intra-assignment delay cannot yet suspend after RHS evaluation. |
| `FSIM-ELAB-047` | error | Assignment target and expression widths differ. |
| `FSIM-ELAB-048` | error | A VHDL `if` condition does not have scalar Boolean type. |
| `FSIM-ELAB-049` | error | Binary operands have different widths and would require implicit sizing. |
| `FSIM-ELAB-050` | error | Assignment into a two-state target would implicitly lose four- or nine-state values. |
| `FSIM-ELAB-051` | error | A VHDL assertion condition does not have scalar Boolean type. |
| `FSIM-ELAB-052` | error | A local variable has no executable packed width. |
| `FSIM-ELAB-053` | error | A local variable duplicates another local or shadows a signal in the bounded slice. |
| `FSIM-ELAB-054` | error | A local variable initializer has the wrong packed width. |
| `FSIM-ELAB-055` | error | A nested procedural block variable reached lowering without supported scope semantics. |
| `FSIM-ELAB-056` | error | A local variable assignment is delayed or nonblocking. |
| `FSIM-ELAB-057` | error | A local variable assignment has the wrong packed width. |
| `FSIM-ELAB-058` | error | A local variable initializer or assignment would implicitly lose four- or nine-state values. |
| `FSIM-ELAB-059` | error | A dynamic wait names an unknown signal. |
| `FSIM-ELAB-060` | error | An edge-qualified dynamic wait names a nonscalar signal. |
| `FSIM-ELAB-061` | error | A wildcard process has no readable signal dependency. |
| `FSIM-ELAB-062` | error | A dynamic wildcard event control has no readable signal dependency. |
| `FSIM-ELAB-063` | error | A case-item choice width does not match its selector width. |
| `FSIM-ELAB-064` | error | A conditional-expression condition is not scalar in the bounded executable slice. |
| `FSIM-ELAB-065` | error | Conditional-expression alternatives have different widths. |
| `FSIM-ELAB-066` | error | A VHDL relational comparison mixes explicitly signed and unsigned packed operands without conversion. |
| `FSIM-ELAB-067` | error | A VHDL arithmetic expression mixes explicitly signed and unsigned packed operands without conversion. |
| `FSIM-ELAB-068` | error | A bit/part select does not have constant in-range bounds compatible with its declared packed direction. |
| `FSIM-ELAB-069` | error | A concatenation is empty or has an operand/result width that cannot be inferred or represented. |
| `FSIM-ELAB-DRV-001` | error | A signal has multiple process drivers, but driver-slot resolution is not executable. |
| `FSIM-ELAB-HIER-001` | error | Duplicate elaborated instance path. |
| `FSIM-ELAB-HIER-002` | error | Recursive instantiation was detected. |
| `FSIM-ELAB-TYPE-001` | error | A signal type cannot be represented by the packed runtime. |

### Bindings and mixed-language boundaries

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-ELAB-BIND-0001` | error | A manifest binding target is not language-qualified. |
| `FSIM-ELAB-BIND-0002` | error | A manifest binding uses an unsupported language. |
| `FSIM-ELAB-BIND-0003` | error | A manifest binding target unit was not found. |
| `FSIM-ELAB-BIND-0004` | error | A manifest binding resolver is neither `std_logic` nor `sv_wire`. |
| `FSIM-ELAB-BIND-010` | error | More than one binding names the same instance path. |
| `FSIM-ELAB-BIND-011` | error | A binding path was not found in the elaborated hierarchy. |
| `FSIM-ELAB-BIND-012` | error | An instance target was not found in the same language and needs an explicit cross-language binding. |
| `FSIM-ELAB-BIND-013` | error | An elaboration binding target is malformed. |
| `FSIM-ELAB-BIND-014` | error | A SystemC binding reached HDL target selection without a matching preconstructed typed factory instance. |
| `FSIM-ELAB-BIND-015` | error | An explicit binding target was not found. |
| `FSIM-ELAB-BIND-016` | error | An explicit VHDL binding does not name an architecture. |
| `FSIM-ELAB-BIND-019` | error | A boundary port or actual uses an unsupported value domain. |
| `FSIM-ELAB-BIND-020` | error | Boundary port and actual widths differ. |
| `FSIM-ELAB-BIND-021` | error | Boundary port and actual signedness differ. |
| `FSIM-ELAB-BIND-022` | error | A boundary connection would implicitly lose state when converted to a two-state destination. |
| `FSIM-ELAB-BIND-023` | error | Conflicting resolvers were assigned to one boundary net. |
| `FSIM-ELAB-BIND-024` | error | Multiple boundary drivers lack the required explicit resolver. |
| `FSIM-ELAB-BIND-025` | error | A named port is unknown or there are too many positional connections. |
| `FSIM-ELAB-BIND-026` | error | An instance port is connected more than once. |
| `FSIM-ELAB-BIND-027` | error | A boundary actual is not a whole signal. |
| `FSIM-ELAB-BIND-028` | error | A boundary actual names an unknown parent signal. |
| `FSIM-ELAB-BIND-029` | error | A resolved multi-driver boundary still requires unimplemented driver-slot resolution. |
| `FSIM-ELAB-BIND-030` | error | A cross-language `inout` lacks an explicit resolver. |
| `FSIM-ELAB-BIND-031` | error | A cross-language `inout` has a resolver but still requires unimplemented driver-slot resolution. |
| `FSIM-ELAB-BIND-032` | error | More than one preconstructed SystemC description names the same instance path. |
| `FSIM-ELAB-BIND-033` | error | A preconstructed SystemC instance was not reached from the selected top. |
| `FSIM-ELAB-BIND-034` | error | A SystemC foreign child declares a port absent from its bound HDL target. |
| `FSIM-ELAB-BIND-035` | error | A SystemC foreign-child port is connected more than once. |
| `FSIM-ELAB-BIND-036` | error | A SystemC foreign-child port references an unknown registered object. |
| `FSIM-ELAB-BIND-037` | error | A SystemC foreign-child port direction differs from its bound HDL target. |
| `FSIM-ELAB-BIND-038` | error | An HDL-to-SystemC binding has no matching preconstructed factory instance. |
| `FSIM-ELAB-BIND-039` | error | A preconstructed SystemC instance target differs from its manifest binding. |
| `FSIM-ELAB-BIND-040` | error | A SystemC foreign child lacks its required explicit HDL binding. |
| `FSIM-ELAB-BIND-041` | error | A SystemC foreign child is bound to a non-HDL target. |

## Time, runtime, trace, and design cache

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-TIME-0001` | error | The project resolution cannot be converted to a legal VCD timescale. |
| `FSIM-TIME-0002` | error | The configured run duration is invalid or not representable at project resolution. |
| `FSIM-TIME-0003` | error | An HDL delay is invalid or not representable at project resolution. |
| `FSIM-TIME-0004` | error | A declared SystemVerilog time precision is not representable at project resolution. |
| `FSIM-RUN-0001` | error | The SimIR interpreter failed during a CLI run. |
| `FSIM-RUN-0002` | error | Another exception terminated a CLI run. |
| `FSIM-RUN-ASSERT-0001` | assertion severity | A false HDL assertion stopped a CLI simulation; the diagnostic retains its source location and message. |
| `FSIM-RUN-DELTA-0001` | error | Simulation exceeded `max_deltas`; the message includes pending processes and recent signals. |
| `FSIM-VCD-0001` | error | The trace output directory could not be created. |
| `FSIM-VCD-0002` | error | The VCD trace file could not be opened. |
| `FSIM-VCD-0003` | error | VCD declaration, value emission, timestamp scaling, or flushing failed. |
| `FSIM-CACHE-0001` | error | A parsed source could not be hashed or associated with its cache provenance. |
| `FSIM-CACHE-0002` | warning | An unreadable or incompatible design-cache entry was discarded. |
| `FSIM-CACHE-0003` | error | A design-cache entry could not be populated. |
| `FSIM-CACHE-0004` | warning | Native-object cache load or store failures prevented complete cache reuse. |

## SystemC source compiler and plug-in validation

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-SC-A001` | error | A validated SystemC plug-in registered no module factory. |
| `FSIM-SC-A002` | error | A requested SystemC hierarchy has no compiled plug-in or registered factory. |
| `FSIM-SC-A003` | error | A requested SystemC factory uses the legacy untyped construction ABI. |
| `FSIM-SC-A004` | error | A typed SystemC factory failed during module construction. |
| `FSIM-SC-A005` | error | One SystemC instance path has conflicting factory targets. |
| `FSIM-SC-C001` | error | The SystemC compiler working directory cannot be resolved. |
| `FSIM-SC-C002` | error | A SystemC source set contains no C++ sources. |
| `FSIM-SC-C003` | error | A SystemC source or dependency is missing, unreadable, invalid, or cannot be hashed/scanned. |
| `FSIM-SC-C004` | error | A compiler option conflicts with fsim's shared-library output. |
| `FSIM-SC-C005` | error | A SystemC plug-in cache or lock directory cannot be created. |
| `FSIM-SC-C006` | error | The per-key SystemC plug-in cache lock cannot be acquired. |
| `FSIM-SC-C007` | error | The host compiler is unavailable, failed to start, failed, or produced no shared library. |
| `FSIM-SC-C008` | error | A compiled plug-in or checksum cannot be hashed, written, or atomically published. |
| `FSIM-SC-C009` | error | A compiled SystemC plug-in failed ABI or entry-point validation. |
| `FSIM-SC-C010` | error | An explicit linked library is missing or cannot be content-hashed. |
| `FSIM-SC-C011` | error | A raw compiler option hides inputs from the persistent cache dependency model. |
| `FSIM-SC-C012` | warning | Dependency closure or a volatile predefined macro prevents safe persistent plug-in cache reuse. |
| `FSIM-SC-C013` | error | A tracked plug-in input or compiler identity changed during compilation; the unpublished output was discarded. |

## Consistency check

The `fsim.diagnostics-catalog` CTest scans literal `FSIM-*` identifiers in
`src/` and `include/`, removes the three cache-format markers named above, and
requires the resulting set to match this catalog exactly. It catches a new,
removed, or renamed literal code that is not reflected here.

The check is intentionally lexical. It cannot discover a diagnostic code
assembled dynamically at runtime, nor can it prove that the documented
severity and explanation still match every control-flow path. Current
production emitters use literal or constant literal codes, so the complete
current set is covered.
