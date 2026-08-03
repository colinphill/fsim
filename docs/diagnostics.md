<!-- SPDX-License-Identifier: Apache-2.0 -->
# Diagnostic code catalog

This catalog describes every literal diagnostic code emitted by the current
production sources. It documents the internal vertical slice, not the eventual
complete v1 implementation. A code identifies a diagnostic class; paths,
source locations, messages, and notes provide the instance-specific detail.
Code spellings are the stable, machine-readable part of the current
diagnostic interface; message wording may evolve.

Source and manifest paths carried by text or JSON diagnostics are normalized
UTF-8 generic paths on Linux and Windows. A leading UTF-8 BOM is transport
metadata rather than a token; CRLF and CR line endings retain the same logical
line accounting as LF while physical byte offsets remain source-accurate.

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
| `FSIM-TCL-0001` | error | The selected build has no embedded Tcl interface. |
| `FSIM-TCL-0002` | error | The Tcl interpreter, standard library, fsim namespace, or standard channels could not be initialized. |
| `FSIM-TCL-0003` | error | A batch Tcl command or script failed during evaluation. |
| `FSIM-TCL-0004` | error | Interactive input ended with an incomplete Tcl command. |
| `FSIM-TCL-ASSERT-0001` | note/warning/error/fatal | An HDL assertion failed while a Tcl-controlled simulation was running. |
| `FSIM-TCL-REPORT-0001` | note/warning/error/fatal | A VHDL report or SystemVerilog severity task was delivered to Tcl diagnostics and callbacks. |
| `FSIM-API-0002` | error | A C API check or build was requested before loading a project. |
| `FSIM-API-0003` | error | A C API operation requires a successfully built design. |
| `FSIM-API-0004` | error | A built design contains more debug-visible local variables than the version-1 object-handle encoding can represent. |
| `FSIM-API-ASSERT-0001` | assertion severity | A false HDL assertion stopped simulation through the C API; the diagnostic carries its process, source location, severity, and message. |
| `FSIM-API-REPORT-0001` | report severity | A nonfatal VHDL report was delivered through the C API assertion callback. |
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
| `FSIM-SEM-0001` | error | Source analysis or build normalization produced an internally invalid owning semantic projection. |
| `FSIM-FE-CU-0001` | warning | A VHDL source set requested Verilog-style compilation-unit grouping; VHDL files remain independent analysis units. |
| `FSIM-FE-VHORDER-001` | error | A VHDL architecture appears before its entity in manifest analysis order. |
| `FSIM-FE-VHORDER-002` | error | A VHDL package body appears before its matching package declaration. |
| `FSIM-FE-VHORDER-003` | error | A VHDL context reference names a context that has not yet been analyzed. |
| `FSIM-FE-VHORDER-004` | error | A VHDL use clause names a project package that has not yet been analyzed. |
| `FSIM-FE-VHORDER-005` | error | A VHDL configuration declaration appears before its configured entity. |
| `FSIM-FE-VHORDER-006` | error | A VHDL configuration declaration or binding names an architecture that has not yet been analyzed. |
| `FSIM-FE-VHORDER-007` | error | A VHDL entity binding names an entity that has not yet been analyzed. |
| `FSIM-FE-VHORDER-008` | error | A VHDL configuration binding names a configuration that has not yet been analyzed. |
| `FSIM-FE-VHSTD-001` | error | A required compiler-supplied IEEE 1076-2019 source file is unavailable or unreadable. |
| `FSIM-FE-VHSTD-002` | error | A compiler-supplied IEEE 1076-2019 source file does not match its pinned upstream checksum. |
| `FSIM-FE-VHSTD-003` | error | fsim's intrinsic semantic projection of a pinned IEEE package is internally invalid. |
| `FSIM-FE-VHSTD-004` | error | A project source attempts to redeclare a compiler-supplied IEEE package. |
| `FSIM-ELAB-VHNUM-001` | error | A bounded IEEE numeric function has the wrong arity or value profile. |
| `FSIM-ELAB-VHNUM-002` | error | A numeric conversion or resize result size is not locally static in 1 through 64. |
| `FSIM-ELAB-VHNUM-003` | error | `to_integer` exceeds the bounded signed or unsigned input-width profile. |
| `FSIM-ELAB-VHNUM-004` | error | A numeric conversion or resize result size differs from its contextual width. |
| `FSIM-ELAB-VHLOGIC-001` | error | A bounded standard-logic conversion or predicate has an unsupported argument count. |
| `FSIM-ELAB-VHLOGIC-002` | error | A standard-logic conversion has an unsupported scalar/vector width, state domain, or contextual result width. |
| `FSIM-ELAB-VHLOGIC-003` | error | A standard-logic mapping or string conversion is outside the supported static mapping profile. |
| `FSIM-ELAB-VHFIX-001` | error | A bounded fixed-point conversion or resize has the wrong arity, operand type, or value profile. |
| `FSIM-ELAB-VHFIX-002` | error | A fixed-point result range is not locally static, descending, and from 1 through 64 bits. |
| `FSIM-ELAB-VHFIX-003` | error | A fixed-point conversion, scale expansion, or rounded resize is outside the bounded static/default profile. |
| `FSIM-ELAB-VHFIX-004` | error | Fixed-point conversion or resize bounds differ from the contextual fixed-point range. |
| `FSIM-ELAB-VHFLT-001` | error | A bounded floating conversion has an unsupported argument or value profile. |
| `FSIM-ELAB-VHFLT-002` | error | A floating result does not have the reviewed binary32 `float(8 downto -23)` context. |
| `FSIM-ELAB-VHFLT-003` | error | Floating arithmetic or vector conversion is not locally static in the bounded binary32 profile. |
| `FSIM-ELAB-VHFLT-004` | error | Floating-to-integer conversion has a NaN, infinity, or out-of-range result. |
| `FSIM-FE-IO-001` | error | An HDL source file could not be opened. |
| `FSIM-FE-IO-002` | error | Reading an HDL source file failed after it was opened. |
| `FSIM-FE-IO-003` | error | HDL language inference failed for the source-file extension. |
| `FSIM-FE-LEX-001` | error | The lexer encountered an unexpected character. |
| `FSIM-FE-LEX-002` | error | A Verilog/SystemVerilog block comment is unterminated. |
| `FSIM-FE-LEX-003` | error | An extended identifier is unterminated. |
| `FSIM-FE-LEX-004` | error | A string literal is unterminated. |
| `FSIM-FE-PARSE-001` | error | A parser expectation using the common fallback code failed. |
| `FSIM-FE-PP-0001` | error | Include directories or macro definitions were supplied for a VHDL source set; these settings apply only to Verilog/SystemVerilog or SystemC. |

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
| `FSIM-VHDL-PARSE-031` | error | Expected `)` after a slice. |
| `FSIM-VHDL-PARSE-032` | error | Expected `)` after an index. |
| `FSIM-VHDL-PARSE-033` | error | Expected `)` after call arguments. |
| `FSIM-VHDL-PARSE-034` | error | Expected `)` after a parenthesized expression. |
| `FSIM-VHDL-PARSE-035` | error | Expected a VHDL expression. |
| `FSIM-VHDL-PARSE-036` | error | Expected `)` after an architecture name in an entity aspect. |
| `FSIM-VHDL-PARSE-037` | error | Expected `map` after `generic`. |
| `FSIM-VHDL-PARSE-038` | error | Expected `(` after `generic map`. |
| `FSIM-VHDL-PARSE-040` | error | Expected `map` after `port`. |
| `FSIM-VHDL-PARSE-041` | error | Expected `(` after `port map`. |
| `FSIM-VHDL-PARSE-042` | error | Expected `)` after port associations. |
| `FSIM-VHDL-PARSE-043` | error | Expected `;` after a VHDL instance. |
| `FSIM-VHDL-PARSE-044` | error | A VHDL library, use, or context-reference clause is malformed or unterminated. |
| `FSIM-VHDL-PARSE-046` | error | Expected `;` after an assertion. |
| `FSIM-VHDL-PARSE-047` | error | Expected `:` after process-variable names. |
| `FSIM-VHDL-PARSE-048` | error | Expected `;` after a process-variable declaration. |
| `FSIM-VHDL-PARSE-049` | error | Expected `;` after a wait statement. |
| `FSIM-VHDL-PARSE-050` | error | Expected `(` after an entity `generic` clause. |
| `FSIM-VHDL-PARSE-051` | error | Expected `:` after generic names. |
| `FSIM-VHDL-PARSE-052` | error | Expected `;` between generic declarations. |
| `FSIM-VHDL-PARSE-053` | error | Expected `)` after generic declarations. |
| `FSIM-VHDL-PARSE-054` | error | Expected `;` after a generic clause. |
| `FSIM-VHDL-PARSE-055` | error | Expected `)` after a generic map. |
| `FSIM-VHDL-PARSE-056` | error | Expected `generate` after a VHDL if-generate condition. |
| `FSIM-VHDL-PARSE-057` | error | Expected `generate` after `else` in a conditional generate. |
| `FSIM-VHDL-PARSE-058` | error | Expected `end` for a conditional generate. |
| `FSIM-VHDL-PARSE-059` | error | Expected `generate` after the conditional-generate `end`. |
| `FSIM-VHDL-PARSE-060` | error | A conditional-generate end label does not match its opening label. |
| `FSIM-VHDL-PARSE-061` | error | Expected `;` after a conditional generate. |
| `FSIM-VHDL-PARSE-062` | error | Expected `in` after a VHDL generate-loop variable. |
| `FSIM-VHDL-PARSE-063` | error | Expected `to` or `downto` in a VHDL generate iteration range. |
| `FSIM-VHDL-PARSE-064` | error | Expected `generate` after a VHDL generate iteration range. |
| `FSIM-VHDL-PARSE-065` | error | Expected `end` for an iterative generate. |
| `FSIM-VHDL-PARSE-066` | error | Expected `generate` after the iterative-generate `end`. |
| `FSIM-VHDL-PARSE-067` | error | An iterative-generate end label does not match its opening label. |
| `FSIM-VHDL-PARSE-068` | error | Expected `;` after an iterative generate. |
| `FSIM-VHDL-PARSE-069` | error | Expected `generate` after a VHDL case-generate selector. |
| `FSIM-VHDL-PARSE-070` | error | A case-generate alternative has no stable label. |
| `FSIM-VHDL-PARSE-071` | error | A case-generate alternative label or `when` introducer is malformed. |
| `FSIM-VHDL-PARSE-072` | error | A case-generate alternative has no choice. |
| `FSIM-VHDL-PARSE-073` | error | Expected `=>` after case-generate choices. |
| `FSIM-VHDL-PARSE-074` | error | Expected `end` for a case generate. |
| `FSIM-VHDL-PARSE-075` | error | Expected `generate` after the case-generate `end`. |
| `FSIM-VHDL-PARSE-076` | error | A case-generate end label does not match its opening label. |
| `FSIM-VHDL-PARSE-077` | error | Expected `;` after a case generate. |
| `FSIM-VHDL-PARSE-078` | error | Expected `begin` in a VHDL block statement. |
| `FSIM-VHDL-PARSE-079` | error | Expected `end` for a VHDL block statement. |
| `FSIM-VHDL-PARSE-080` | error | Expected `block` after the block-statement `end`. |
| `FSIM-VHDL-PARSE-081` | error | A block-statement end label does not match its opening label. |
| `FSIM-VHDL-PARSE-082` | error | Expected `;` after a VHDL block statement. |
| `FSIM-VHDL-PARSE-083` | error | Expected `:` after generated constant names. |
| `FSIM-VHDL-PARSE-084` | error | A generated constant lacks its required `:=` default expression. |
| `FSIM-VHDL-PARSE-085` | error | Expected `;` after a generated constant declaration. |
| `FSIM-VHDL-PARSE-086` | error | Expected `is` after a VHDL package name. |
| `FSIM-VHDL-PARSE-087` | error | Expected `:` after package constant names. |
| `FSIM-VHDL-PARSE-088` | error | A package constant lacks its required `:=` default expression. |
| `FSIM-VHDL-PARSE-089` | error | Expected `;` after a package constant declaration. |
| `FSIM-VHDL-PARSE-090` | error | Expected `is` after a VHDL context declaration name. |
| `FSIM-VHDL-PARSE-091` | error | Expected `end` for a VHDL context declaration. |
| `FSIM-VHDL-PARSE-092` | error | A context declaration end name does not match its opening name. |
| `FSIM-VHDL-PARSE-093` | error | Expected `;` after a VHDL context declaration. |
| `FSIM-VHDL-PARSE-094` | error | Expected `is` after a sequential VHDL case selector. |
| `FSIM-VHDL-PARSE-095` | error | Expected `when` before a sequential VHDL case alternative. |
| `FSIM-VHDL-PARSE-096` | error | Expected `=>` after sequential VHDL case choices. |
| `FSIM-VHDL-PARSE-097` | error | Expected `end` for a sequential VHDL case statement. |
| `FSIM-VHDL-PARSE-098` | error | Expected `case` after the sequential statement's `end`. |
| `FSIM-VHDL-PARSE-099` | error | Expected `;` after a sequential VHDL case statement. |
| `FSIM-VHDL-PARSE-100` | error | Expected `in` after a sequential VHDL for-loop parameter. |
| `FSIM-VHDL-PARSE-101` | error | Expected `to` or `downto` in a sequential VHDL for-loop range. |
| `FSIM-VHDL-PARSE-102` | error | Expected `loop` after a sequential VHDL for-loop range. |
| `FSIM-VHDL-PARSE-103` | error | Expected `end` for a sequential VHDL for loop. |
| `FSIM-VHDL-PARSE-104` | error | Expected `loop` after the sequential for-loop body. |
| `FSIM-VHDL-PARSE-105` | error | Expected `;` after a sequential VHDL for loop. |
| `FSIM-VHDL-PARSE-106` | error | Expected `loop` after a VHDL while condition. |
| `FSIM-VHDL-PARSE-107` | error | Expected `end` for a sequential VHDL while loop. |
| `FSIM-VHDL-PARSE-108` | error | Expected `loop` after a sequential VHDL while-loop body. |
| `FSIM-VHDL-PARSE-109` | error | Expected `;` after a sequential VHDL while loop. |
| `FSIM-VHDL-PARSE-110` | error | Expected `;` after a VHDL `exit` or `next` statement. |
| `FSIM-VHDL-PARSE-111` | error | Expected `end` after an unconditional VHDL sequential-loop body. |
| `FSIM-VHDL-PARSE-112` | error | Expected `loop` after `end` for an unconditional VHDL sequential loop. |
| `FSIM-VHDL-PARSE-113` | error | Expected `;` after an unconditional VHDL sequential loop. |
| `FSIM-VHDL-PARSE-114` | error | A VHDL exponent has an unparenthesized sign or an exponentiation chain omits required parentheses. |
| `FSIM-VHDL-PARSE-115` | error | A VHDL conditional assignment is missing its `else` alternative. |
| `FSIM-VHDL-PARSE-116` | error | Expected `select` after a VHDL selected-assignment selector. |
| `FSIM-VHDL-PARSE-117` | error | Expected `<=` before selected-assignment waveforms. |
| `FSIM-VHDL-PARSE-118` | error | Expected `when` after a selected-assignment waveform. |
| `FSIM-VHDL-PARSE-119` | error | Expected `;` after a selected signal assignment. |
| `FSIM-VHDL-PARSE-120` | error | Expected `)` after a VHDL array-attribute dimension. |
| `FSIM-VHDL-PARSE-122` | error | Expected `;` after a VHDL `report` statement. |
| `FSIM-VHDL-PARSE-123` | error | Expected `inertial` after a VHDL `reject` time. |
| `FSIM-VHDL-PARSE-124` | error | A VHDL delay mechanism appears anywhere other than immediately after `<=`. |
| `FSIM-VHDL-PARSE-125` | error | `unaffected` is mixed with other elements instead of forming a complete waveform alternative. |
| `FSIM-VHDL-PARSE-126` | error | A VHDL waveform is empty or lacks a value. |
| `FSIM-VHDL-PARSE-127` | error | Expected `is` in a VHDL type declaration. |
| `FSIM-VHDL-PARSE-128` | error | Expected `:` after VHDL record element names. |
| `FSIM-VHDL-PARSE-129` | error | Expected `;` after a VHDL record element declaration. |
| `FSIM-VHDL-PARSE-130` | error | A bounded VHDL record declaration has no supported elements. |
| `FSIM-VHDL-PARSE-131` | error | A VHDL record declaration has a malformed `end record` clause. |
| `FSIM-VHDL-PARSE-132` | error | An aggregate choice is not a record element, `others`, or a supported locally static integer expression or range. |
| `FSIM-VHDL-PARSE-133` | error | An aggregate association has no value or a trailing comma has no following association. |
| `FSIM-VHDL-PARSE-134` | error | A VHDL subtype declaration is missing `is`. |
| `FSIM-VHDL-PARSE-135` | error | A VHDL subtype declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-136` | error | A VHDL enumeration literal is neither an identifier nor a character literal. |
| `FSIM-VHDL-PARSE-137` | error | A VHDL enumeration declaration is missing a comma between literals. |
| `FSIM-VHDL-PARSE-138` | error | A VHDL enumeration declaration has a trailing comma. |
| `FSIM-VHDL-PARSE-139` | error | A VHDL enumeration declaration contains no valid literals. |
| `FSIM-VHDL-PARSE-140` | error | A VHDL enumeration declaration is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-141` | error | A VHDL enumeration declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-142` | error | A one-argument VHDL enumeration scalar attribute is missing its parenthesized argument. |
| `FSIM-VHDL-PARSE-143` | error | A VHDL array declaration is missing its opening parenthesis. |
| `FSIM-VHDL-PARSE-144` | error | An unconstrained VHDL array index has a malformed `<>` box. |
| `FSIM-VHDL-PARSE-145` | error | A constrained VHDL array index is missing `to` or `downto`. |
| `FSIM-VHDL-PARSE-146` | error | A VHDL array index definition is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-147` | error | A VHDL array declaration is missing `of` before its element subtype. |
| `FSIM-VHDL-PARSE-148` | error | A VHDL array type declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-149` | error | A VHDL aggregate range choice is missing its right bound. |
| `FSIM-VHDL-PARSE-150` | error | A VHDL aggregate choice list has no choice after `|`. |
| `FSIM-VHDL-PARSE-151` | error | A VHDL aggregate range or choice list is not followed by `=>`. |
| `FSIM-VHDL-PARSE-152` | error | A VHDL function formal declaration is missing `:` after its names. |
| `FSIM-VHDL-PARSE-153` | error | VHDL function formal declarations are not separated by `;`. |
| `FSIM-VHDL-PARSE-154` | error | A VHDL function formal list is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-155` | error | A VHDL function specification is missing `return`. |
| `FSIM-VHDL-PARSE-156` | error | An interface-function box default has a malformed `<>`. |
| `FSIM-VHDL-PARSE-157` | error | An interface function's `is` clause has neither a function name nor `<>`. |
| `FSIM-VHDL-PARSE-158` | error | A VHDL function body is missing `is`. |
| `FSIM-VHDL-PARSE-159` | error | A VHDL function-local variable declaration is missing `:`. |
| `FSIM-VHDL-PARSE-160` | error | A VHDL function-local variable declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-161` | error | A VHDL function body is missing `begin`. |
| `FSIM-VHDL-PARSE-162` | error | A VHDL function body is missing `end`. |
| `FSIM-VHDL-PARSE-163` | error | A VHDL function body is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-164` | error | A VHDL function return statement has no result expression. |
| `FSIM-VHDL-PARSE-165` | error | A VHDL return statement is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-166` | error | A bounded VHDL package body is missing `end`. |
| `FSIM-VHDL-PARSE-167` | error | A bounded VHDL package body is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-168` | error | A VHDL procedure parameter declaration is missing the colon before its mode and subtype. |
| `FSIM-VHDL-PARSE-169` | error | Adjacent VHDL procedure parameter declarations are missing a separating semicolon. |
| `FSIM-VHDL-PARSE-170` | error | A VHDL procedure parameter list is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-171` | error | An interface-procedure box default is missing the closing `>`. |
| `FSIM-VHDL-PARSE-172` | error | An interface-procedure `is` default is neither a procedure name nor `<>`. |
| `FSIM-VHDL-PARSE-173` | error | A VHDL procedure body is missing `is`. |
| `FSIM-VHDL-PARSE-174` | error | A VHDL procedure-local variable declaration is missing its colon. |
| `FSIM-VHDL-PARSE-175` | error | A VHDL procedure-local variable declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-176` | error | A VHDL procedure body is missing `begin`. |
| `FSIM-VHDL-PARSE-177` | error | A VHDL procedure body is missing `end`. |
| `FSIM-VHDL-PARSE-178` | error | A VHDL procedure body is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-179` | error | A VHDL procedure call is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-180` | error | A VHDL procedure call is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-181` | error | A package generic-map aspect is missing `map`. |
| `FSIM-VHDL-PARSE-182` | error | A package generic-map aspect is missing its opening parenthesis. |
| `FSIM-VHDL-PARSE-183` | error | A whole-map package generic box is missing its closing `>`. |
| `FSIM-VHDL-PARSE-184` | error | A whole-map package generic box is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-185` | error | A defaulted package generic association is missing its closing `>`. |
| `FSIM-VHDL-PARSE-186` | error | A package generic association list is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-187` | error | An interface package declaration is missing `is`. |
| `FSIM-VHDL-PARSE-188` | error | An interface package declaration is missing `new`. |
| `FSIM-VHDL-PARSE-189` | error | An interface package declaration is missing its `generic map` aspect. |
| `FSIM-VHDL-PARSE-190` | error | A local package instantiation is missing `is`. |
| `FSIM-VHDL-PARSE-191` | error | A local package instantiation is missing `new`. |
| `FSIM-VHDL-PARSE-192` | error | A local package instantiation is missing its `generic map` aspect. |
| `FSIM-VHDL-PARSE-193` | error | A local package instantiation is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-194` | error | A generic subprogram instantiation is missing `is`. |
| `FSIM-VHDL-PARSE-195` | error | A generic subprogram instantiation is missing `new`. |
| `FSIM-VHDL-PARSE-196` | error | A generic subprogram aspect is missing `map`. |
| `FSIM-VHDL-PARSE-197` | error | A generic subprogram map is missing its opening parenthesis. |
| `FSIM-VHDL-PARSE-198` | error | A whole-map generic subprogram box is missing its closing `>`. |
| `FSIM-VHDL-PARSE-199` | error | A whole-map generic subprogram box is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-200` | error | A defaulted generic subprogram association is missing its closing `>`. |
| `FSIM-VHDL-PARSE-201` | error | A generic subprogram association list is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-202` | error | A generic subprogram instantiation is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-203` | error | A generic clause intended for a subprogram is not followed by a function or procedure. |
| `FSIM-VHDL-PARSE-204` | error | A configuration declaration is missing `of`. |
| `FSIM-VHDL-PARSE-205` | error | A configuration declaration is missing `is`. |
| `FSIM-VHDL-PARSE-206` | error | A configuration declaration is missing its top architecture block introduced by `for`. |
| `FSIM-VHDL-PARSE-207` | error | A component configuration or specification is missing the colon after its instantiation list. |
| `FSIM-VHDL-PARSE-208` | error | A component configuration or specification is missing `use`. |
| `FSIM-VHDL-PARSE-209` | error | A bounded configuration binding indication is missing `entity`. |
| `FSIM-VHDL-PARSE-210` | error | A configured entity aspect has no parenthesized architecture name. |
| `FSIM-VHDL-PARSE-211` | error | A configuration port-map aspect is missing `map`. |
| `FSIM-VHDL-PARSE-212` | error | A configuration port-map aspect is missing its opening parenthesis. |
| `FSIM-VHDL-PARSE-213` | error | A configuration port-map aspect is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-214` | error | A configuration binding indication is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-215` | error | A component configuration is missing `end for`. |
| `FSIM-VHDL-PARSE-216` | error | A component configuration is missing the semicolon after `end for`. |
| `FSIM-VHDL-PARSE-217` | error | An architecture block configuration is missing `end for`. |
| `FSIM-VHDL-PARSE-218` | error | An architecture block configuration is missing the semicolon after `end for`. |
| `FSIM-VHDL-PARSE-219` | error | A configuration declaration is missing `end`. |
| `FSIM-VHDL-PARSE-220` | error | A configuration declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-221` | error | A component port clause is missing its opening parenthesis. |
| `FSIM-VHDL-PARSE-222` | error | A component port declaration is missing the colon after its names. |
| `FSIM-VHDL-PARSE-223` | error | A component port declaration is missing a supported mode. |
| `FSIM-VHDL-PARSE-224` | error | Component port declarations are not separated by semicolons. |
| `FSIM-VHDL-PARSE-225` | error | A component port clause is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-226` | error | A component port clause is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-227` | error | A component declaration is missing its end clause. |
| `FSIM-VHDL-PARSE-228` | error | A component declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-229` | error | An indexed generate block configuration is missing its closing parenthesis. |
| `FSIM-VHDL-PARSE-230` | error | A default-box generic association is missing its closing angle bracket. |
| `FSIM-VHDL-PARSE-231` | error | Expected `)` after a VHDL block guard expression. |
| `FSIM-VHDL-PARSE-232` | error | Expected `;` after a VHDL block generic-map aspect. |
| `FSIM-VHDL-PARSE-233` | error | Expected `;` after a VHDL block port-map aspect. |
| `FSIM-VHDL-PARSE-234` | error | Expected `begin` after a VHDL generate declarative part. |
| `FSIM-VHDL-PARSE-235` | error | Expected `function` after a `pure` or `impure` prefix in a VHDL generate declarative part. |
| `FSIM-VHDL-PARSE-236` | error | A bounded VHDL object alias is missing `is`, its target, or its terminating semicolon. |
| `FSIM-VHDL-PARSE-237` | error | A VHDL access type declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-238` | error | A VHDL physical type range is missing `to` or `downto`. |
| `FSIM-VHDL-PARSE-239` | error | A VHDL physical type declaration is missing `units`. |
| `FSIM-VHDL-PARSE-240` | error | A secondary physical unit is missing its physical-literal scale. |
| `FSIM-VHDL-PARSE-241` | error | A physical unit declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-242` | error | A VHDL physical type declaration is missing `end`. |
| `FSIM-VHDL-PARSE-243` | error | A VHDL physical type end clause is missing `units`. |
| `FSIM-VHDL-PARSE-244` | error | A VHDL physical type declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-245` | error | A protected private-variable declaration is missing the colon after its names. |
| `FSIM-VHDL-PARSE-246` | error | A protected private-variable declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-247` | error | A VHDL protected declaration or body is missing `end`. |
| `FSIM-VHDL-PARSE-248` | error | A VHDL protected end clause is missing `protected`. |
| `FSIM-VHDL-PARSE-249` | error | A VHDL protected-body end clause is missing `body`. |
| `FSIM-VHDL-PARSE-250` | error | A VHDL protected declaration or body is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-251` | error | A dereferenced index is missing its closing parenthesis, or a shared declaration is missing `variable`. |
| `FSIM-VHDL-PARSE-252` | error | A VHDL shared-variable declaration is missing the colon after its names. |
| `FSIM-VHDL-PARSE-253` | error | A VHDL shared-variable declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-254` | error | A VHDL file type declaration is missing `of` before its element subtype. |
| `FSIM-VHDL-PARSE-255` | error | A VHDL file type declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-256` | error | A VHDL file object declaration is missing the colon after its names. |
| `FSIM-VHDL-PARSE-257` | error | A VHDL file object `open` clause is missing its open-kind expression. |
| `FSIM-VHDL-PARSE-258` | error | A VHDL file object `is` clause is missing its logical-name expression. |
| `FSIM-VHDL-PARSE-259` | error | A VHDL file object open-kind expression is not followed by `is` and a logical name. |
| `FSIM-VHDL-PARSE-260` | error | A VHDL file object declaration is missing its terminating semicolon. |
| `FSIM-VHDL-PARSE-261` | error | A VHDL report statement or assertion report clause is missing its expression. |
| `FSIM-VHDL-PARSE-262` | error | A VHDL severity clause is missing its expression. |

### VHDL semantics and bounded-subset rejections

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-VHDL-SEM-002` | error | Duplicate port declaration. |
| `FSIM-VHDL-SEM-003` | error | Duplicate signal declaration. |
| `FSIM-VHDL-SEM-012` | error | A process combines a sensitivity list with an explicit wait statement. |
| `FSIM-VHDL-SEM-013` | error | A generic name is declared more than once. |
| `FSIM-VHDL-SEM-014` | error | A generic conflicts with a port or signal in the same declarative namespace. |
| `FSIM-VHDL-SEM-015` | error | A named generic actual is repeated in one map. |
| `FSIM-VHDL-SEM-016` | error | A positional generic actual follows a named actual. |
| `FSIM-VHDL-SEM-017` | error | A case generate contains more than one `others` alternative. |
| `FSIM-VHDL-SEM-018` | error | An `others` case-generate alternative is not last. |
| `FSIM-VHDL-SEM-019` | error | A generated constant is duplicated or conflicts with a generated signal. |
| `FSIM-VHDL-SEM-020` | error | A package constant name is declared more than once. |
| `FSIM-VHDL-SEM-021` | error | A sequential VHDL case statement contains more than one `others` alternative. |
| `FSIM-VHDL-SEM-022` | error | A sequential VHDL case statement has an alternative after `others`. |
| `FSIM-VHDL-SEM-023` | error | A VHDL `exit` or `next` statement appears outside a sequential loop. |
| `FSIM-VHDL-SEM-024` | error | A VHDL `exit` or `next` targets a loop label that is not visible. |
| `FSIM-VHDL-SEM-025` | error | An `end loop` label is orphaned or does not match its opening label. |
| `FSIM-VHDL-SEM-026` | error | A VHDL loop label duplicates another sequential label in the enclosing process. |
| `FSIM-VHDL-SEM-027` | error | A selected assignment contains more than one `others` alternative. |
| `FSIM-VHDL-SEM-028` | error | A selected-assignment alternative follows `others`. |
| `FSIM-VHDL-SEM-029` | error | The bounded selected-assignment form has no final `others` alternative. |
| `FSIM-VHDL-SEM-030` | error | The selected VHDL attribute is outside the bounded supported array-attribute set. |
| `FSIM-VHDL-SEM-031` | error | A VHDL rejection limit is negative. |
| `FSIM-VHDL-SEM-032` | error | A VHDL rejection limit exceeds the first waveform-element delay. |
| `FSIM-VHDL-SEM-033` | error | A VHDL `reject` clause is paired with the transport delay mechanism. |
| `FSIM-VHDL-SEM-034` | error | VHDL waveform-element delays are not strictly ascending after exact project-time normalization. |
| `FSIM-VHDL-SEM-035` | error | A VHDL record declares the same case-insensitive element name more than once. |
| `FSIM-VHDL-SEM-036` | error | A VHDL design unit declares the same bounded type name more than once. |
| `FSIM-VHDL-SEM-037` | error | A VHDL record's optional end name does not match its declaration name. |
| `FSIM-VHDL-SEM-038` | error | A positional aggregate association follows a named association. |
| `FSIM-VHDL-SEM-039` | error | An aggregate contains multiple `others` associations or does not place `others` last. |
| `FSIM-VHDL-SEM-040` | error | A VHDL enumeration declares the same identifier or character literal more than once. |
| `FSIM-VHDL-SEM-041` | error | `others` is combined with another choice in one VHDL aggregate association. |
| `FSIM-VHDL-SEM-042` | error | A VHDL function profile declares the same formal name more than once. |
| `FSIM-VHDL-SEM-043` | error | A VHDL function declaration appears in a region that requires a body. |
| `FSIM-VHDL-SEM-044` | error | A VHDL function local duplicates the result, a formal, or another local. |
| `FSIM-VHDL-SEM-045` | error | A VHDL function end name does not match its designator. |
| `FSIM-VHDL-SEM-046` | error | A VHDL return statement appears outside a function body. |
| `FSIM-VHDL-SEM-047` | error | A package-body end name does not match its package name. |
| `FSIM-VHDL-SEM-048` | error | A bounded VHDL function body has no explicit return statement. |
| `FSIM-VHDL-SEM-049` | error | A constant-class VHDL procedure parameter has an output or inout mode. |
| `FSIM-VHDL-SEM-050` | error | A VHDL procedure profile declares the same formal name more than once. |
| `FSIM-VHDL-SEM-051` | error | A VHDL procedure declaration appears in a region that requires a body. |
| `FSIM-VHDL-SEM-052` | error | A VHDL procedure local duplicates a formal or another local. |
| `FSIM-VHDL-SEM-053` | error | A VHDL procedure end name does not match its designator. |
| `FSIM-VHDL-SEM-054` | error | A VHDL procedure return statement carries a value. |
| `FSIM-VHDL-SEM-055` | error | A positional VHDL procedure-call actual follows a named actual. |
| `FSIM-VHDL-SEM-056` | error | A bounded VHDL procedure body assigns to a constant-class formal. |
| `FSIM-VHDL-SEM-057` | error | A package generic-map aspect associates the same named formal more than once. |
| `FSIM-VHDL-SEM-058` | error | A positional package generic association follows a named association. |
| `FSIM-VHDL-SEM-059` | error | A declarative region contains the same local package-instance name more than once. |
| `FSIM-VHDL-SEM-060` | error | A generic subprogram map associates the same named formal more than once. |
| `FSIM-VHDL-SEM-061` | error | A positional generic subprogram association follows a named association. |
| `FSIM-VHDL-SEM-062` | error | A generic subprogram instance duplicates another subprogram declaration or instance. |
| `FSIM-VHDL-SEM-063` | error | A generic subprogram template has no generic formal. |
| `FSIM-VHDL-SEM-064` | error | A declarative region contains a duplicate generic function or procedure template. |
| `FSIM-VHDL-SEM-065` | error | A configuration instantiation list contains the same component label more than once. |
| `FSIM-VHDL-SEM-066` | error | `all` or `others` is combined with another configuration instantiation-list choice. |
| `FSIM-VHDL-SEM-067` | error | A configuration declaration's end name does not match its opening name. |
| `FSIM-VHDL-SEM-068` | error | One declarative region repeats an exact component profile. |
| `FSIM-VHDL-SEM-069` | error | A component declaration's end name does not match its opening name. |
| `FSIM-VHDL-SEM-071` | error | A component generic or port formal is declared more than once or conflicts with another component formal. |
| `FSIM-VHDL-SEM-072` | error | A component port default is declared on a non-input formal. |
| `FSIM-VHDL-SEM-073` | error | A positional VHDL function-call actual follows a named actual. |
| `FSIM-VHDL-SEM-074` | error | A VHDL procedure parameter default is declared on a non-input formal. |
| `FSIM-VHDL-SEM-075` | error | A VHDL entity port default is declared on a non-input formal. |
| `FSIM-VHDL-SEM-076` | error | A VHDL value generic is declared with a nonconstant object class. |
| `FSIM-VHDL-SEM-077` | error | A VHDL value generic is declared with a mode other than input. |
| `FSIM-VHDL-SEM-078` | error | A named port actual is repeated in one instance map. |
| `FSIM-VHDL-SEM-079` | error | A positional port actual follows a named actual. |
| `FSIM-VHDL-SEM-081` | error | A generated VHDL declarative item conflicts with an earlier declaration from a different non-overloadable family. |
| `FSIM-VHDL-SEM-082` | error | A process or subprogram local declarative item conflicts with an earlier declaration from a different non-overloadable family. |
| `FSIM-VHDL-SEM-083` | error | A bounded VHDL declarative region repeats an object alias name. |
| `FSIM-VHDL-SEM-084` | error | A sequential `if`/`case` or process end label is orphaned or does not match its opening label. |
| `FSIM-VHDL-SEM-085` | error | A `process(all)` sensitivity clause also contains an explicit sensitivity name. |
| `FSIM-VHDL-SEM-086` | error | A VHDL matching case does not use `?` consistently after its opening and ending `case` keywords. |
| `FSIM-VHDL-SEM-087` | error | The `guarded` keyword appears on a sequential rather than concurrent signal assignment. |
| `FSIM-VHDL-SEM-088` | error | A VHDL physical type repeats a unit name. |
| `FSIM-VHDL-SEM-089` | error | A VHDL physical type declares no primary unit. |
| `FSIM-VHDL-SEM-090` | error | A VHDL physical type end name does not match its declaration name. |
| `FSIM-VHDL-SEM-091` | error | A VHDL protected body repeats a private-variable name. |
| `FSIM-VHDL-SEM-092` | error | A VHDL protected declaration or body end name does not match its declaration name. |
| `FSIM-VHDL-SEM-093` | error | A VHDL declarative region repeats a shared-variable name. |
| `FSIM-VHDL-SEM-094` | error | A VHDL declarative region repeats a file-object name. |
| `FSIM-VHDL-SEM-095` | error | A VHDL file interface declaration incorrectly specifies a parameter mode. |
| `FSIM-VHDL-SEM-096` | error | A VHDL file interface declaration incorrectly specifies a default expression. |
| `FSIM-VHDL-UNSUPPORTED-001` | error | Unsupported design unit or context item. |
| `FSIM-VHDL-UNSUPPORTED-003` | error | Unsupported entity declaration. |
| `FSIM-VHDL-UNSUPPORTED-004` | error | Unsupported architecture declaration. |
| `FSIM-VHDL-UNSUPPORTED-005` | error | Unsupported labeled concurrent statement. |
| `FSIM-VHDL-UNSUPPORTED-006` | error | Unsupported concurrent statement. |
| `FSIM-VHDL-UNSUPPORTED-007` | error | A process declarative item is outside the bounded constant, type/subtype, variable, alias, package-instance, and local-callable subset. |
| `FSIM-VHDL-UNSUPPORTED-008` | error | Unsupported sequential statement. |
| `FSIM-VHDL-UNSUPPORTED-012` | error | Signal initializers are parsed but not executable. |
| `FSIM-VHDL-UNSUPPORTED-014` | error | An integer-family subtype appears in a declaration context that does not yet admit scalar integer objects. |
| `FSIM-VHDL-UNSUPPORTED-015` | error | A nested context declaration appears where only a context reference is permitted. |
| `FSIM-VHDL-UNSUPPORTED-018` | error | A generic type is outside the bounded scalar integer, Boolean, bit, and physical-time subset. |
| `FSIM-VHDL-UNSUPPORTED-020` | error | A generate branch contains an item outside the bounded constant, local-signal, assignment, process, instance, and nested-generate subset. |
| `FSIM-VHDL-UNSUPPORTED-022` | error | A package declaration item is outside the bounded constant, type, subtype, or function subset. |
| `FSIM-VHDL-UNSUPPORTED-023` | error | A package constant is outside the scalar integer, Boolean, or bit subset. |
| `FSIM-VHDL-UNSUPPORTED-024` | error | A context declaration contains an item other than a library clause, use clause, or context reference. |
| `FSIM-VHDL-UNSUPPORTED-025` | error | A `null` waveform element appears outside a guarded concurrent signal assignment. |
| `FSIM-VHDL-UNSUPPORTED-026` | error | A bounded VHDL type or record element is outside the architecture-local, non-nested packed record subset. |
| `FSIM-VHDL-UNSUPPORTED-027` | error | A VHDL array declaration uses an index subtype outside the retained `integer`, `natural`, or `positive` subset, or applies multiple constraints to a built-in scalar/vector subtype. |
| `FSIM-VHDL-UNSUPPORTED-028` | error | A VHDL interface type generic uses classified or default-like syntax outside the VHDL-2008 unclassified `type T` form. |
| `FSIM-VHDL-UNSUPPORTED-029` | error | A bounded VHDL function formal is not constant class. |
| `FSIM-VHDL-UNSUPPORTED-030` | error | A bounded VHDL function formal is not input mode. |
| `FSIM-VHDL-UNSUPPORTED-031` | error | A VHDL function formal type is outside the bounded scalar integral or visible scalar-subtype profile. |
| `FSIM-VHDL-UNSUPPORTED-033` | error | A VHDL interface function uses an operator-symbol designator. |
| `FSIM-VHDL-UNSUPPORTED-034` | error | A VHDL function result type is outside the bounded scalar integral or visible scalar-subtype profile. |
| `FSIM-VHDL-UNSUPPORTED-035` | error | A VHDL function declarative item is outside the bounded local constant, type/subtype, variable, alias, package-instance, and callable subset. |
| `FSIM-VHDL-UNSUPPORTED-037` | error | A bounded VHDL function body contains timing, signal updates, or another unsupported statement. |
| `FSIM-VHDL-UNSUPPORTED-038` | error | A bounded VHDL procedure formal has signal or file class rather than constant or variable class. |
| `FSIM-VHDL-UNSUPPORTED-039` | error | A bounded VHDL procedure formal uses buffer, linkage, or another unsupported mode. |
| `FSIM-VHDL-UNSUPPORTED-040` | error | A VHDL procedure formal type is outside the bounded scalar integral or visible scalar-subtype profile. |
| `FSIM-VHDL-UNSUPPORTED-042` | error | A VHDL procedure uses an operator-symbol designator. |
| `FSIM-VHDL-UNSUPPORTED-043` | error | A VHDL procedure declarative item is outside the bounded local constant, type/subtype, variable, alias, package-instance, and callable subset. |
| `FSIM-VHDL-UNSUPPORTED-044` | error | A bounded VHDL procedure body contains a signal update or another unsupported statement other than a wait. |
| `FSIM-VHDL-UNSUPPORTED-045` | error | A nested interface-package formal appears in a bounded generic subprogram template. |
| `FSIM-VHDL-UNSUPPORTED-046` | error | A generic function instantiation carries a `pure` or `impure` prefix. |
| `FSIM-VHDL-UNSUPPORTED-047` | error | A bounded generic function template is impure. |
| `FSIM-VHDL-UNSUPPORTED-050` | error | A configuration declaration contains an item outside the bounded architecture/component configuration subset. |
| `FSIM-VHDL-UNSUPPORTED-052` | error | A component declaration contains an unsupported declarative item. |
| `FSIM-VHDL-UNSUPPORTED-053` | error | A generated VHDL declarative region contains an item outside the bounded constant, signal, alias, type, subtype, callable, component, and local-package subset. |
| `FSIM-VHDL-UNSUPPORTED-054` | error | A bounded VHDL object alias omits its explicit subtype indication. |
| `FSIM-VHDL-UNSUPPORTED-055` | error | A protected type contains a declarative item outside the bounded private-variable, function, and procedure subset. |

## Verilog and SystemVerilog frontend

### Verilog/SystemVerilog preprocessing

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-SV-PP-001` | error | The Verilog preprocessor was invoked for a non-Verilog language. |
| `FSIM-SV-PP-002` | error | A conditional compilation block is unterminated. |
| `FSIM-SV-PP-003` | error | A command-line or manifest macro definition has an invalid identifier. |
| `FSIM-SV-PP-004` | error | Recursive include processing exceeded the configured depth limit. |
| `FSIM-SV-PP-005` | error | An include cycle was detected. |
| `FSIM-SV-PP-006` | error | `` `elsif`` has no matching conditional opener. |
| `FSIM-SV-PP-007` | error | `` `elsif`` follows `` `else`` in the same conditional block. |
| `FSIM-SV-PP-008` | error | `` `else`` has no matching conditional opener. |
| `FSIM-SV-PP-009` | error | A conditional block contains more than one `` `else``. |
| `FSIM-SV-PP-010` | error | `` `endif`` has no matching conditional opener. |
| `FSIM-SV-PP-011` | error | A recognized compiler directive is outside the currently implemented preprocessing subset. |
| `FSIM-SV-PP-012` | error | A conditional or undefinition directive lacks its required macro identifier. |
| `FSIM-SV-PP-013` | error | Unexpected tokens follow a directive macro identifier. |
| `FSIM-SV-PP-014` | error | A no-argument conditional directive has trailing tokens. |
| `FSIM-SV-PP-015` | error | `` `define`` lacks a macro identifier. |
| `FSIM-SV-PP-016` | error | A function-like macro parameter list is malformed. |
| `FSIM-SV-PP-017` | error | A function-like macro repeats a parameter name. |
| `FSIM-SV-PP-018` | error | A function-like macro parameter list is unterminated. |
| `FSIM-SV-PP-019` | error | `` `include`` lacks a file name. |
| `FSIM-SV-PP-020` | error | A macro-expanded include name produces no tokens. |
| `FSIM-SV-PP-021` | error | An include name is neither a string literal nor an angle-bracket name. |
| `FSIM-SV-PP-022` | error | An include file cannot be resolved from the including file or configured search roots. |
| `FSIM-SV-PP-023` | error | A function-like macro invocation lacks an argument list. |
| `FSIM-SV-PP-024` | error | A function-like macro argument list is unterminated. |
| `FSIM-SV-PP-025` | error | Token concatenation lacks an operand. |
| `FSIM-SV-PP-026` | error | Token concatenation does not form exactly one valid token. |
| `FSIM-SV-PP-027` | error | A backtick is not followed by a macro identifier. |
| `FSIM-SV-PP-028` | error | A macro invocation or compiler directive is undefined. |
| `FSIM-SV-PP-029` | error | Macro expansion is recursive or exceeds the configured depth limit. |
| `FSIM-SV-PP-030` | error | A function-like macro receives the wrong number of arguments. |
| `FSIM-SV-PP-031` | error | A Verilog preprocessing compilation unit contains no root files. |
| `FSIM-SV-PP-032` | error | A no-argument state directive has trailing tokens. |
| `FSIM-SV-PP-033` | error | `` `endcelldefine`` has no active `` `celldefine`` state. |
| `FSIM-SV-PP-034` | error | `` `default_nettype`` has a missing, invalid, or trailing value. |
| `FSIM-SV-PP-035` | error | `` `unconnected_drive`` does not contain exactly `pull0` or `pull1`. |
| `FSIM-SV-PP-036` | error | `` `begin_keywords`` has a missing, unsupported, or trailing IEEE version string. |
| `FSIM-SV-PP-037` | error | `` `end_keywords`` has no matching `` `begin_keywords``. |
| `FSIM-SV-PP-038` | error | A `` `begin_keywords`` region is unterminated. |
| `FSIM-SV-PP-039` | error | A `` `line`` directive does not contain exactly a line number, quoted file name, and level. |
| `FSIM-SV-PP-040` | error | A `` `line`` line number is not a positive representable decimal integer. |
| `FSIM-SV-PP-041` | error | A `` `line`` logical file name is not a string literal. |
| `FSIM-SV-PP-042` | error | A `` `line`` logical file name contains an invalid escape. |
| `FSIM-SV-PP-043` | error | A `` `line`` level is not `0`, `1`, or `2`. |
| `FSIM-SV-PP-044` | error | A macro is redefined with a different parameter list, defaults, or replacement. |
| `FSIM-SV-PP-045` | error | A conditional compilation block opened inside an include remains open when that include ends. |
| `FSIM-SV-PP-046` | error | An include attempts to continue or close a conditional block opened by its parent source. |
| `FSIM-SV-PP-047` | error | `` `celldefine`` is nested or repeated while already active. |
| `FSIM-SV-PP-048` | error | `` `nounconnected_drive`` appears without active `` `unconnected_drive`` state. |

### Verilog/SystemVerilog syntax

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-SV-PARSE-001` | error | Expected a Verilog/SystemVerilog identifier. |
| `FSIM-SV-PARSE-002` | error | Expected `)` after module ports. |
| `FSIM-SV-PARSE-003` | error | Expected `;` after a module header. |
| `FSIM-SV-PARSE-004` | error | Expected `endmodule`. |
| `FSIM-SV-PARSE-005` | error | Expected `:` in a packed range. |
| `FSIM-SV-PARSE-006` | error | Expected `]` after a packed range. |
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
| `FSIM-SV-PARSE-137` | error | Expected `(` after `assert`. |
| `FSIM-SV-PARSE-138` | error | Expected `)` after an assertion condition. |
| `FSIM-SV-PARSE-139` | error | Expected `;` after a SystemVerilog function return statement. |
| `FSIM-SV-PARSE-140` | error | Expected `)` after a bounded SystemVerilog function argument list. |
| `FSIM-SV-PARSE-141` | error | Expected `;` after a bounded SystemVerilog function header. |
| `FSIM-SV-PARSE-142` | error | Expected `endfunction` after a bounded SystemVerilog function body. |
| `FSIM-SV-PARSE-143` | error | Expected `)` after a bounded SystemVerilog task argument list. |
| `FSIM-SV-PARSE-144` | error | Expected `;` after a bounded SystemVerilog task header. |
| `FSIM-SV-PARSE-145` | error | Expected `endtask` after a bounded SystemVerilog task body. |
| `FSIM-SV-PARSE-146` | error | Expected `)` after a bounded SystemVerilog task call argument list. |
| `FSIM-SV-PARSE-147` | error | Expected `;` after a bounded SystemVerilog task call. |
| `FSIM-SV-PARSE-148` | error | Expected `(` after `$fclose`. |
| `FSIM-SV-PARSE-149` | error | Expected `)` after a `$fclose` handle. |
| `FSIM-SV-PARSE-150` | error | Expected `;` after `$fclose`. |
| `FSIM-SV-PARSE-151` | error | Expected `(` after `$fdisplay` or `$fwrite`. |
| `FSIM-SV-PARSE-152` | error | Expected `,` after a file-output handle. |
| `FSIM-SV-PARSE-153` | error | Expected `)` after file-output arguments. |
| `FSIM-SV-PARSE-154` | error | Expected `;` after `$fdisplay` or `$fwrite`. |
| `FSIM-SV-PARSE-155` | error | Expected `]` after a SystemVerilog queue dimension. |
| `FSIM-SV-PARSE-156` | error | Expected `;` after a supported container method call. |
| `FSIM-SV-PARSE-157` | error | Expected `]` after a SystemVerilog associative-array index type. |
| `FSIM-SV-PARSE-158` | error | Expected `]` after a SystemVerilog static unpacked-array range. |
| `FSIM-SV-PARSE-159` | error | Expected `(` after a `$readmem*` or `$writemem*` task name. |
| `FSIM-SV-PARSE-160` | error | Expected `,` after a memory-file task file name. |
| `FSIM-SV-PARSE-161` | error | Expected `)` after memory-file task arguments. |
| `FSIM-SV-PARSE-162` | error | Expected `;` after a memory-file task. |
| `FSIM-SV-PARSE-163` | error | Expected `{` after a SystemVerilog assignment-pattern apostrophe. |
| `FSIM-SV-PARSE-164` | error | Expected `}` after a SystemVerilog assignment pattern. |
| `FSIM-SV-PARSE-165` | error | Expected `(` after a predicate container locator's `with` keyword. |
| `FSIM-SV-PARSE-166` | error | Expected `)` after a predicate container locator expression. |
| `FSIM-SV-PARSE-167` | error | Expected `(` after a container reduction's `with` keyword. |
| `FSIM-SV-PARSE-168` | error | Expected `)` after a container reduction transformation. |
| `FSIM-SV-PARSE-169` | error | Expected `(` after a container ordering method's `with` keyword. |
| `FSIM-SV-PARSE-170` | error | Expected `)` after a container ordering key expression. |
| `FSIM-SV-PARSE-171` | error | Expected `(` after an extrema/uniqueness locator's transformation `with` keyword. |
| `FSIM-SV-PARSE-172` | error | Expected `)` after an extrema/uniqueness locator transformation. |
| `FSIM-SV-PARSE-173` | error | Expected `:` after a SystemVerilog assignment-pattern `default` choice. |
| `FSIM-SV-PARSE-174` | error | Expected a value after a SystemVerilog assignment-pattern association. |
| `FSIM-SV-PARSE-175` | error | The `inside` membership operator is used outside SystemVerilog. |
| `FSIM-SV-PARSE-176` | error | Expected the braced list after an `inside` membership operator. |
| `FSIM-SV-PARSE-177` | error | An `inside` membership list is empty. |
| `FSIM-SV-PARSE-178` | error | Expected `:` between the low and high bounds of an `inside` range. |
| `FSIM-SV-PARSE-179` | error | Expected `]` after an `inside` range. |
| `FSIM-SV-PARSE-180` | error | A `case inside` statement is used outside SystemVerilog. |
| `FSIM-SV-PARSE-181` | error | Expected `:` between the low and high bounds of a `case inside` range. |
| `FSIM-SV-PARSE-182` | error | Expected `]` after a `case inside` range. |
| `FSIM-SV-PARSE-183` | error | A `case inside` alternative contains no choice or a trailing empty choice. |
| `FSIM-SV-PARSE-184` | error | `inside` matching is combined with `casez` or `casex`. |
| `FSIM-SV-PARSE-185` | error | A `unique`, `unique0`, or `priority` case qualifier is used outside SystemVerilog. |
| `FSIM-SV-PARSE-186` | error | A case statement contains more than one qualifier. |
| `FSIM-SV-PARSE-187` | error | A `case matches` statement is used outside SystemVerilog. |
| `FSIM-SV-PARSE-188` | error | Bounded `matches` pattern matching is combined with `casez` or `casex`. |
| `FSIM-SV-PARSE-189` | error | A bounded `case matches` item contains a comma-separated pattern list instead of exactly one pattern. |
| `FSIM-SV-PARSE-190` | error | A `case matches` dot pattern is missing its wildcard or variable name. |
| `FSIM-SV-PARSE-191` | error | Expected the inner `{` before streaming-concatenation operands. |
| `FSIM-SV-PARSE-192` | error | A streaming concatenation contains no operand. |
| `FSIM-SV-PARSE-193` | error | Expected the inner `}` after streaming-concatenation operands. |
| `FSIM-SV-PARSE-194` | error | Expected the outer `}` after a streaming concatenation. |
| `FSIM-SV-PARSE-195` | error | Expected `=` in a procedural force statement. |
| `FSIM-SV-PARSE-196` | error | Expected `;` after a procedural force or release statement. |
| `FSIM-SV-PARSE-197` | error | Expected `;` after a classic function formal declaration. |
| `FSIM-SV-PARSE-198` | error | Expected `;` after a classic task formal declaration. |
| `FSIM-SV-PARSE-199` | error | Expected `(` after a named function actual. |
| `FSIM-SV-PARSE-200` | error | Expected `)` after a named function actual. |
| `FSIM-SV-PARSE-201` | error | Expected `(` after a named task actual. |
| `FSIM-SV-PARSE-202` | error | Expected `)` after a named task actual. |
| `FSIM-SV-PARSE-203` | error | A repeated intra-assignment event control is missing its opening parenthesis. |
| `FSIM-SV-PARSE-204` | error | A repeated intra-assignment event control is missing its closing parenthesis. |
| `FSIM-SV-PARSE-205` | error | A repeated intra-assignment event control is missing its `@` event marker. |
| `FSIM-SV-PARSE-206` | error | A procedural `fork` block is missing its terminating `join`, `join_any`, or `join_none`. |
| `FSIM-SV-PARSE-207` | error | A `wait fork` statement is missing its terminating semicolon. |
| `FSIM-SV-PARSE-208` | error | A bounded `disable` process-control statement does not select `fork`. |
| `FSIM-SV-PARSE-209` | error | A `disable fork` statement is missing its terminating semicolon. |
| `FSIM-SV-PARSE-210` | error | A static gate-instance array range is missing its colon. |
| `FSIM-SV-PARSE-211` | error | A static gate-instance array range is missing its closing bracket. |
| `FSIM-SV-PARSE-212` | error | A modport declaration is missing the opening parenthesis after its name. |
| `FSIM-SV-PARSE-213` | error | A modport declaration is missing its closing parenthesis. |
| `FSIM-SV-PARSE-214` | error | A modport declaration is missing its terminating semicolon. |
| `FSIM-SV-PARSE-215` | error | A package export item is missing `::` after its package selector. |
| `FSIM-SV-PARSE-216` | error | A package export declaration is missing its terminating semicolon. |
| `FSIM-SV-PARSE-217` | error | A static module/interface instance array range is missing its colon. |
| `FSIM-SV-PARSE-218` | error | A static module/interface instance array range is missing its closing bracket. |
| `FSIM-SV-PARSE-219` | error | Expected `(` after a SystemVerilog cast type. |
| `FSIM-SV-PARSE-220` | error | Expected `)` after a SystemVerilog cast expression. |
| `FSIM-SV-PARSE-221` | error | Expected `:` in a multidimensional static unpacked range. |
| `FSIM-SV-PARSE-222` | error | Expected `]` after a multidimensional static unpacked range. |
| `FSIM-SV-PARSE-223` | error | Expected `)` after a dynamic-array `new[size](initializer)` expression. |
| `FSIM-SV-PARSE-044` | error | Expected an immediate-assertion pass or failure action statement. |
| `FSIM-SV-PARSE-045` | error | Expected `;` after a procedural variable declaration. |
| `FSIM-SV-PARSE-046` | error | Expected `(` after `case`. |
| `FSIM-SV-PARSE-047` | error | Expected `)` after a `case` selector expression. |
| `FSIM-SV-PARSE-048` | error | Expected `:` after a `case` item. |
| `FSIM-SV-PARSE-049` | error | Expected `endcase`. |
| `FSIM-SV-PARSE-050` | error | A value parameter has no default constant expression. |
| `FSIM-SV-PARSE-051` | error | Expected `;` after a body parameter declaration. |
| `FSIM-SV-PARSE-052` | error | Expected `(` after a module parameter `#`. |
| `FSIM-SV-PARSE-053` | error | A module parameter-port item does not begin with `parameter` or `localparam`. |
| `FSIM-SV-PARSE-054` | error | Expected `)` after a module parameter-port list. |
| `FSIM-SV-PARSE-055` | error | Expected `(` after an instance parameter `#`. |
| `FSIM-SV-PARSE-056` | error | Expected `(` after a named parameter override. |
| `FSIM-SV-PARSE-057` | error | Expected `)` after a named parameter override. |
| `FSIM-SV-PARSE-058` | error | Expected `)` after instance parameter overrides. |
| `FSIM-SV-PARSE-059` | error | Expected `(` after a generate `if`. |
| `FSIM-SV-PARSE-060` | error | Expected `)` after a generate condition. |
| `FSIM-SV-PARSE-061` | error | A conditional-generate branch is not a labeled `begin`/`end` block. |
| `FSIM-SV-PARSE-063` | error | A generate branch lacks `end` or has a mismatched end label. |
| `FSIM-SV-PARSE-064` | error | Expected `endgenerate`. |
| `FSIM-SV-PARSE-065` | error | Expected `(` after a generate `for`. |
| `FSIM-SV-PARSE-066` | error | An executable generate-loop variable has neither an inline nor module-scope `genvar` declaration. |
| `FSIM-SV-PARSE-067` | error | Expected `=` after a generate-loop variable. |
| `FSIM-SV-PARSE-068` | error | Expected `;` after a generate-loop initializer. |
| `FSIM-SV-PARSE-069` | error | Expected `;` after a generate-loop condition. |
| `FSIM-SV-PARSE-070` | error | A generate-loop iteration assigns a name other than its loop variable. |
| `FSIM-SV-PARSE-071` | error | A generate-loop iteration is not an assignment, prefix/postfix increment/decrement, `+=`, or `-=` update. |
| `FSIM-SV-PARSE-072` | error | Expected `)` after a generate-loop header. |
| `FSIM-SV-PARSE-073` | error | Expected `(` after a generate `case`. |
| `FSIM-SV-PARSE-074` | error | Expected `)` after a generate-case selector. |
| `FSIM-SV-PARSE-075` | error | Expected `:` after generate-case choices. |
| `FSIM-SV-PARSE-076` | error | Expected `endcase` for a generate case. |
| `FSIM-SV-PARSE-077` | error | Expected `;` after a module-scope `genvar` declaration. |
| `FSIM-SV-PARSE-078` | error | Expected `::` after a package name in an import clause. |
| `FSIM-SV-PARSE-079` | error | Expected `;` after a package import. |
| `FSIM-SV-PARSE-080` | error | Expected `;` after a SystemVerilog package header. |
| `FSIM-SV-PARSE-081` | error | Expected `endpackage` for a package declaration. |
| `FSIM-SV-PARSE-082` | error | Expected `;` after a bounded SystemVerilog typedef declaration. |
| `FSIM-SV-PARSE-083` | error | Expected `{` before the literals of a bounded enum typedef. |
| `FSIM-SV-PARSE-084` | error | Expected `}` after the literals of a bounded enum typedef. |
| `FSIM-SV-PARSE-085` | error | A bounded enum typedef has no literals. |
| `FSIM-SV-PARSE-086` | error | Expected `{` before bounded packed-aggregate members. |
| `FSIM-SV-PARSE-087` | error | Expected `}` after bounded packed-aggregate members. |
| `FSIM-SV-PARSE-088` | error | Expected `;` after a packed-aggregate member declaration. |
| `FSIM-SV-PARSE-089` | error | A bounded packed aggregate has no members. |
| `FSIM-SV-PARSE-090` | error | A replication concatenation has no repeated operands. |
| `FSIM-SV-PARSE-091` | error | Expected `(` after a built-in gate primitive and its optional instance name. |
| `FSIM-SV-PARSE-092` | error | A built-in gate primitive does not begin its terminal list with an output lvalue. |
| `FSIM-SV-PARSE-093` | error | Expected `)` after a built-in gate primitive terminal list. |
| `FSIM-SV-PARSE-094` | error | Expected `;` after a built-in gate primitive instance. |
| `FSIM-SV-PARSE-095` | error | Expected `(` after a procedural `for`. |
| `FSIM-SV-PARSE-096` | error | Expected `=` after an inline procedural loop variable. |
| `FSIM-SV-PARSE-097` | error | Expected `;` after a procedural loop initializer. |
| `FSIM-SV-PARSE-098` | error | Expected `;` after a procedural loop condition. |
| `FSIM-SV-PARSE-099` | error | Expected `)` after a procedural loop header. |
| `FSIM-SV-PARSE-100` | error | Expected `(` after a procedural `repeat`. |
| `FSIM-SV-PARSE-101` | error | Expected `)` after a procedural repeat count. |
| `FSIM-SV-PARSE-102` | error | Expected `(` after a procedural `while`. |
| `FSIM-SV-PARSE-103` | error | Expected `)` after a procedural while condition. |
| `FSIM-SV-PARSE-104` | error | Expected `;` after a SystemVerilog `break` or `continue` statement. |
| `FSIM-SV-PARSE-105` | error | Expected `while` after a SystemVerilog `do` body. |
| `FSIM-SV-PARSE-106` | error | Expected `(` before a SystemVerilog do-while condition. |
| `FSIM-SV-PARSE-107` | error | Expected `)` after a SystemVerilog do-while condition. |
| `FSIM-SV-PARSE-108` | error | Expected `;` after a SystemVerilog do-while statement. |
| `FSIM-SV-PARSE-109` | error | Expected `(` after a Verilog/SystemVerilog `wait`. |
| `FSIM-SV-PARSE-110` | error | Expected `)` after a Verilog/SystemVerilog wait condition. |
| `FSIM-SV-PARSE-111` | error | Expected `final` at the start of a SystemVerilog final procedure. |
| `FSIM-SV-PARSE-112` | error | Expected `)` after a Verilog/SystemVerilog `$stop` argument. |
| `FSIM-SV-PARSE-113` | error | Expected `;` after a Verilog/SystemVerilog `$stop` task. |
| `FSIM-SV-PARSE-115` | error | Expected `)` after SystemVerilog `$fatal` arguments. |
| `FSIM-SV-PARSE-116` | error | Expected `;` after a SystemVerilog `$fatal` task. |
| `FSIM-SV-PARSE-117` | error | Expected `;` after a named-event declaration. |
| `FSIM-SV-PARSE-118` | error | Expected `;` after an immediate named-event trigger. |
| `FSIM-SV-PARSE-119` | error | Expected `)` after Verilog/SystemVerilog `$display` arguments. |
| `FSIM-SV-PARSE-120` | error | Expected `;` after a Verilog/SystemVerilog `$display` task. |
| `FSIM-SV-PARSE-121` | error | Expected `)` after Verilog/SystemVerilog `$write` arguments. |
| `FSIM-SV-PARSE-122` | error | Expected `;` after a Verilog/SystemVerilog `$write` task. |
| `FSIM-SV-PARSE-123` | error | Expected `)` after Verilog/SystemVerilog `$strobe` arguments. |
| `FSIM-SV-PARSE-124` | error | Expected `;` after a Verilog/SystemVerilog `$strobe` task. |
| `FSIM-SV-PARSE-125` | error | Expected `)` after Verilog/SystemVerilog `$monitor` arguments. |
| `FSIM-SV-PARSE-126` | error | Expected `;` after a Verilog/SystemVerilog `$monitor` task. |
| `FSIM-SV-PARSE-127` | error | Expected `)` after a Verilog/SystemVerilog `$monitoron` or `$monitoroff` task. |
| `FSIM-SV-PARSE-128` | error | Expected `;` after a Verilog/SystemVerilog `$monitoron` or `$monitoroff` task. |
| `FSIM-SV-PARSE-129` | error | Expected `)` after SystemVerilog `$info`, `$warning`, or `$error` arguments. |
| `FSIM-SV-PARSE-130` | error | Expected `;` after a SystemVerilog `$info`, `$warning`, or `$error` task. |
| `FSIM-SV-PARSE-131` | error | Expected a numeric magnitude in a SystemVerilog time declaration. |
| `FSIM-SV-PARSE-132` | error | Expected a physical unit in a SystemVerilog time declaration. |
| `FSIM-SV-PARSE-133` | error | Expected `;` after a SystemVerilog time declaration. |
| `FSIM-SV-PARSE-134` | error | Expected the second `:` and maximum value in a `min:typ:max` delay triple. |
| `FSIM-SV-PARSE-135` | error | Expected another delay value after a comma in a parenthesized transition-delay list. |
| `FSIM-SV-PARSE-136` | error | An event control has an empty event-expression list. |
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
| `FSIM-VERILOG-SEM-004` | error | A SystemVerilog-only wildcard equality operator was used in Verilog-2005. |
| `FSIM-VERILOG-SEM-005` | error | Compound assignments or standalone increment/decrement were used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-VERILOG-SEM-006` | error | A `final` procedure was used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-VERILOG-SEM-007` | error | `$fatal` was used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-VERILOG-SEM-008` | error | A nonblocking named-event trigger (`->>`) was used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-VERILOG-SEM-009` | error | `$info`, `$warning`, or `$error` was used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-VERILOG-SEM-010` | error | An increment or decrement expression was used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-VERILOG-SEM-011` | error | A procedural force or release statement was used in Verilog-2005 rather than SystemVerilog. |
| `FSIM-SV-SEM-002` | error | A delay magnitude is not a decimal literal. |
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
| `FSIM-SV-SEM-015` | error | An undeclared implicit net is forbidden by `` `default_nettype none``. |
| `FSIM-SV-SEM-016` | error | An untyped ANSI or non-ANSI port is forbidden by `` `default_nettype none``. |
| `FSIM-SV-SEM-017` | error | A parameter name is declared more than once in its module or generate scope. |
| `FSIM-SV-SEM-018` | error | A named parameter override is repeated on one instance. |
| `FSIM-SV-SEM-019` | error | Named and positional parameter overrides are mixed on one instance. |
| `FSIM-SV-SEM-020` | error | A parameter conflicts with a port or signal declaration in the same module or generate namespace. |
| `FSIM-SV-SEM-021` | error | A generate case contains more than one `default` item. |
| `FSIM-SV-SEM-022` | error | A module-scope `genvar` declaration is duplicated or conflicts with another object. |
| `FSIM-SV-SEM-023` | error | A package declaration end name does not match its opening name. |
| `FSIM-SV-SEM-024` | error | A bounded scope declares the same typedef name more than once. |
| `FSIM-SV-SEM-025` | error | A bounded packed aggregate declares the same member name more than once. |
| `FSIM-SV-SEM-026` | error | A bounded built-in gate primitive has an invalid number of input terminals. |
| `FSIM-SV-SEM-027` | error | A bounded procedural loop condition does not compare its loop variable against an integral bound. |
| `FSIM-SV-SEM-028` | error | A bounded procedural loop iteration updates a name other than its loop variable. |
| `FSIM-SV-SEM-029` | error | A bounded procedural loop update is not a positive constant step toward its comparison bound. |
| `FSIM-SV-SEM-103` | error | A procedural loop update is not an assignment or increment of its loop variable. |
| `FSIM-SV-SEM-104` | error | An edge-qualified event expression is not a direct scalar signal in the bounded expression-control slice. |
| `FSIM-SV-SEM-105` | error | A general packed event expression is mixed with another event-list item. |
| `FSIM-SV-SEM-106` | error | A body-timed `always` process has a reachable re-entry path without suspension or termination. |
| `FSIM-SV-SEM-107` | error | `wait fork` or `disable fork` is used outside SystemVerilog-2017 input. |
| `FSIM-SV-SEM-108` | error | `join_any` or `join_none` is used outside SystemVerilog-2017 input. |
| `FSIM-SV-SEM-109` | error | A procedural fork closing label has no opening label or does not match it. |
| `FSIM-SV-SEM-110` | error | A net-declaration delay is attached to a variable rather than a `wire` net. |
| `FSIM-SV-SEM-111` | error | A static gate-instance array has no instance name. |
| `FSIM-SV-SEM-112` | error | A static gate-instance array bound is not a decimal locally static integer in the bounded slice. |
| `FSIM-SV-SEM-113` | error | A static gate-instance array exceeds the 64-instance bound. |
| `FSIM-SV-SEM-114` | error | A gate-array terminal is neither scalar nor equal in width to the instance count. |
| `FSIM-SV-SEM-115` | error | A modport declaration appears outside a SystemVerilog interface. |
| `FSIM-SV-SEM-116` | error | A modport signal member has no explicit direction. |
| `FSIM-SV-SEM-117` | error | A modport repeats a member name. |
| `FSIM-SV-SEM-118` | error | A modport names a signal that is not declared by its interface. |
| `FSIM-SV-SEM-119` | error | An interface repeats a modport declaration name. |
| `FSIM-SV-SEM-120` | error | A bounded instance-array range is not a decimal locally static integer range. |
| `FSIM-SV-SEM-121` | error | A bounded instance array exceeds 64 instances. |
| `FSIM-SV-SEM-122` | error | A modport import/export entry does not name an interface function or task. |
| `FSIM-SV-SEM-123` | error | A modport callable's explicit function/task kind does not match its interface declaration. |
| `FSIM-SV-SEM-124` | error | A generated typedef or enum literal conflicts with another declaration in the same generated body. |
| `FSIM-SV-SEM-125` | error | A type cast appears outside SystemVerilog-2017 input. |
| `FSIM-SV-SEM-126` | error | A bounded static unpacked array declares more than four dimensions. |
| `FSIM-SV-SEM-127` | error | A bounded SystemVerilog string method has the wrong number of arguments. |
| `FSIM-SV-SEM-128` | error | A dynamic-array `new[size](initializer)` expression has other than one initializer. |
| `FSIM-SV-SEM-129` | error | A module or interface declaration end name does not match its opening name. |
| `FSIM-ELAB-SVIFACE-006` | error | A process writes through a read-only input port or modport input member. |
| `FSIM-ELAB-SVIFACE-007` | error | A retained interface callable cannot be materialized at its same-language module boundary. |
| `FSIM-ELAB-SVIFACE-008` | error | An interface callable is visible more than once through the same module port. |
| `FSIM-ELAB-SVIFACE-009` | error | A modport export has no matching callable implementation in the connected module. |
| `FSIM-SV-SEM-030` | error | A reachable `forever` path can take its backedge without suspending, exiting, or terminating the simulation. |
| `FSIM-SV-SEM-031` | error | A SystemVerilog `break` or `continue` statement appears outside a procedural loop. |
| `FSIM-SV-SEM-032` | error | A SystemVerilog `final` procedure contains a timing control, wait, or `$finish`. |
| `FSIM-SV-SEM-033` | error | A SystemVerilog `final` procedure contains a nonblocking assignment. |
| `FSIM-SV-SEM-034` | error | A procedural block closing label has no opening label or does not match it. |
| `FSIM-SV-SEM-035` | error | A named event conflicts with another event, signal, or port declaration. |
| `FSIM-SV-SEM-036` | error | A delayed named-event trigger uses immediate `->` rather than nonblocking `->>` syntax. |
| `FSIM-SV-SEM-037` | error | A `$display` format string has more value-consuming conversions than value arguments. |
| `FSIM-SV-SEM-038` | error | A `$write` format string has more value-consuming conversions than value arguments. |
| `FSIM-SV-SEM-039` | error | A `$strobe` format string has more value-consuming conversions than value arguments. |
| `FSIM-SV-SEM-040` | error | A Verilog/SystemVerilog output string uses an unsupported, incomplete, or out-of-byte-range escape. |
| `FSIM-SV-SEM-041` | error | A `$monitor` format string has more value-consuming conversions than value arguments. |
| `FSIM-SV-SEM-042` | error | An output format string uses an unsupported conversion/modifier, an invalid or overflowing field width, or a malformed percent escape. |
| `FSIM-SV-SEM-043` | error | A bounded `$info`, `$warning`, or `$error` call has a nonliteral or additional message argument. |
| `FSIM-SV-SEM-044` | error | A `timeunit` or `timeprecision` declaration appears in Verilog-2005 input. |
| `FSIM-SV-SEM-045` | error | A time declaration has an illegal magnitude or physical unit. |
| `FSIM-SV-SEM-046` | error | A scope repeats a `timeunit` or `timeprecision` declaration. |
| `FSIM-SV-SEM-047` | error | A compilation-unit or module time declaration appears after another item. |
| `FSIM-SV-SEM-048` | error | An effective SystemVerilog timeprecision is coarser than its timeunit or overflows. |
| `FSIM-SV-SEM-049` | error | A decimal delay literal cannot be represented by the exact bounded rational HIR form. |
| `FSIM-SV-SEM-050` | error | A fractional delay has neither an explicit unit nor an active timeunit/timescale. |
| `FSIM-SV-SEM-051` | error | A Verilog-2005 delay uses a SystemVerilog explicit physical-unit suffix. |
| `FSIM-SV-SEM-052` | error | A Verilog/SystemVerilog `min:typ:max` delay triple is not parenthesized. |
| `FSIM-SV-SEM-053` | error | A Verilog/SystemVerilog delay list supplies more transition values than the containing assignment or gate form permits. |
| `FSIM-SV-SEM-054` | error | A procedural assignment contains more than one delay or event control. |
| `FSIM-SV-SEM-055` | error | A SystemVerilog type parameter and typedef declare the same type-namespace name in one bounded scope. |
| `FSIM-SV-SEM-056` | error | A SystemVerilog return statement appears outside a function. |
| `FSIM-SV-SEM-057` | error | A non-void SystemVerilog function return statement omits its value. |
| `FSIM-SV-SEM-058` | error | A bounded function repeats an argument name or conflicts with its result name. |
| `FSIM-SV-SEM-059` | error | A function closing name does not match its declaration name. |
| `FSIM-SV-SEM-060` | error | A bounded function repeats or conflicts with a local declaration. |
| `FSIM-SV-SEM-061` | error | A bounded function assignment target does not have an identifier root. |
| `FSIM-SV-SEM-062` | error | A bounded function assigns one of its input arguments. |
| `FSIM-SV-SEM-063` | error | A bounded function assignment is nonblocking or contains a procedural timing/event control. |
| `FSIM-SV-SEM-064` | error | A bounded function contains a timing control, event statement, or task statement. |
| `FSIM-SV-SEM-065` | error | A bounded function has no function-name assignment or value-return statement. |
| `FSIM-SV-SEM-066` | error | A module or package declares the same bounded function name more than once. |
| `FSIM-SV-SEM-067` | error | A bounded task repeats an argument name. |
| `FSIM-SV-SEM-068` | error | A bounded task closing name differs from its declaration name. |
| `FSIM-SV-SEM-069` | error | A bounded task local conflicts with an argument or earlier local. |
| `FSIM-SV-SEM-070` | error | A bounded task contains a nonblocking/intra-assignment control, `$stop`, or `$finish`. |
| `FSIM-SV-SEM-071` | error | A bounded task return statement incorrectly supplies a value. |
| `FSIM-SV-SEM-073` | error | A module or package declares the same bounded task name more than once. |
| `FSIM-SV-SEM-074` | error | A bounded text-file system function or task appears outside SystemVerilog-2017 input. |
| `FSIM-SV-SEM-075` | error | A bounded text-file system function has the wrong argument count. |
| `FSIM-SV-SEM-076` | error | `$fdisplay` or `$fwrite` has a nonliteral, malformed, unsupported, or multi-value format. |
| `FSIM-SV-SEM-077` | error | A dynamic array, queue, or associative array is used outside SystemVerilog-2017. |
| `FSIM-SV-SEM-078` | error | An unpacked declaration has an unsupported dimension or associative index type. |
| `FSIM-SV-SEM-079` | error | A bounded container has an unsupported nonintegral element type. |
| `FSIM-SV-SEM-080` | error | A bounded SystemVerilog container declares more than one unpacked dimension. |
| `FSIM-SV-SEM-081` | error | A supported container method has the wrong argument count. |
| `FSIM-SV-SEM-082` | error | An associative array uses a string index type. |
| `FSIM-SV-SEM-083` | error | A `$readmem*` or `$writemem*` task is used outside SystemVerilog-2017. |
| `FSIM-SV-SEM-084` | error | An assignment pattern is used outside SystemVerilog-2017. |
| `FSIM-SV-SEM-085` | error | An unpacked-container reduction method is used outside SystemVerilog-2017. |
| `FSIM-SV-SEM-086` | error | An unpacked-container ordering method is used outside SystemVerilog-2017. |
| `FSIM-SV-SEM-087` | error | An unpacked-container locator method is used outside SystemVerilog-2017. |
| `FSIM-SV-SEM-088` | error | A predicate unpacked-container locator method is used outside SystemVerilog-2017. |
| `FSIM-SV-SEM-089` | error | A predicate unpacked-container locator omits its required nonempty `with` clause. |
| `FSIM-SV-SEM-090` | error | A predicate unpacked-container locator has malformed or multiple iterator binders, or a scoped container iterator leaks outside its `with` expression. |
| `FSIM-SV-SEM-091` | error | An unpacked-container reduction has an empty `with` transformation. |
| `FSIM-SV-SEM-092` | error | A `sort` or `rsort` key has an empty `with` clause, a malformed or multiple iterator binder, or a named binder without a `with` clause. |
| `FSIM-SV-SEM-093` | error | An extrema/uniqueness locator transformation has an empty `with` clause, a malformed or multiple iterator binder, or a named binder without a `with` clause. |
| `FSIM-SV-SEM-094` | error | A reduction transformation has a malformed or multiple iterator binder, or a named binder without a `with` clause. |
| `FSIM-SV-SEM-095` | error | A default function actual is attached to a writable or reference formal instead of an input value formal. |
| `FSIM-SV-SEM-096` | error | A `ref` function formal is declared in a static or implicit-lifetime function. |
| `FSIM-SV-SEM-097` | error | A default task actual is attached to a writable or reference formal instead of an input value formal. |
| `FSIM-SV-SEM-098` | error | A `ref` task formal is declared in a static or implicit-lifetime task. |
| `FSIM-SV-SEM-099` | error | A static or implicit-lifetime callable declares a nested block local outside its supported persistent body scope. |
| `FSIM-SV-SEM-100` | error | A streaming concatenation is used outside SystemVerilog-2017. |
| `FSIM-SV-SEM-101` | error | A bounded `always_ff` process does not have exactly one edge-qualified event. |
| `FSIM-SV-SEM-102` | error | A bounded `always_ff` body contains a nested timing control. |
| `FSIM-SV-UNSUPPORTED-001` | error | Unsupported compilation-unit item. |
| `FSIM-SV-UNSUPPORTED-002` | error | A raw parser input contains a directive that was not consumed by preprocessing. |
| `FSIM-SV-UNSUPPORTED-004` | error | Unsupported module item. |
| `FSIM-SV-UNSUPPORTED-005` | error | A named port connection was used where a module-header declaration is required. |
| `FSIM-SV-UNSUPPORTED-007` | error | Unpacked arrays are not implemented. |
| `FSIM-SV-UNSUPPORTED-008` | error | Unsupported identifier-starting procedural statement. |
| `FSIM-SV-UNSUPPORTED-009` | error | Unsupported procedural statement. |
| `FSIM-SV-UNSUPPORTED-010` | error | ANSI port default expressions are parsed but not executable. |
| `FSIM-SV-UNSUPPORTED-011` | error | Declaration initializers are parsed but not executable. |
| `FSIM-SV-UNSUPPORTED-014` | error | A procedural declaration uses the net-only `wire` type. |
| `FSIM-SV-UNSUPPORTED-017` | error | A `unique`, `unique0`, or `priority` qualifier is not followed by a case statement. |
| `FSIM-SV-UNSUPPORTED-019` | error | Type parameters are not implemented. |
| `FSIM-SV-UNSUPPORTED-020` | error | A parameter data type is outside the supported integral subset. |
| `FSIM-SV-UNSUPPORTED-021` | error | A generate region or branch contains an item outside the bounded integral-parameter, local-signal, continuous assignment, process, instance, and nested-generate subset. |
| `FSIM-SV-UNSUPPORTED-022` | error | A generated local declaration incorrectly uses a module-port direction. |
| `FSIM-SV-UNSUPPORTED-023` | error | A package item is outside the bounded integral parameter/localparam and import subset. |
| `FSIM-SV-UNSUPPORTED-024` | error | A bounded typedef target is not an integral built-in or user-defined type. |
| `FSIM-SV-UNSUPPORTED-025` | error | An unpacked typedef dimension is outside the current packed alias subset. |
| `FSIM-SV-UNSUPPORTED-026` | error | A bounded enum typedef lacks an explicit packed `bit`, `logic`, or `reg` base type. |
| `FSIM-SV-UNSUPPORTED-027` | error | A bounded aggregate declaration uses an unpacked union, which is outside the supported packed-union or unpacked-struct slice. |
| `FSIM-SV-UNSUPPORTED-028` | error | A bounded aggregate member uses a data type outside the packed integral, enum, or nested aggregate subset. |
| `FSIM-SV-UNSUPPORTED-029` | error | A bounded aggregate member has an unpacked dimension or initializer. |
| `FSIM-SV-UNSUPPORTED-030` | error | A built-in gate declaration uses unsupported drive strengths. |
| `FSIM-SV-UNSUPPORTED-035` | error | A bounded function output, inout, or ref formal uses a string or unpacked-container type instead of the supported packed integral type. |
| `FSIM-SV-UNSUPPORTED-040` | error | A MOS, bidirectional-switch, resistive, or pull primitive is outside the bounded v1 gate subset. |
| `FSIM-SV-UNSUPPORTED-041` | error | `reverse` or nondeterministic `shuffle` uses an excluded container-ordering `with` clause. |
| `FSIM-SV-UNSUPPORTED-042` | error | A bounded `case matches` item uses a deferred variable-binding, tagged, or structured pattern. |
| `FSIM-SV-UNSUPPORTED-043` | error | A bounded `case matches` item uses a deferred `&&&` guard. |
| `FSIM-SV-UNSUPPORTED-044` | error | A modport uses a deferred ref, clocking, or callable import/export member instead of a bounded signal direction. |

## Elaboration and SimIR lowering

### Top selection, objects, and executable lowering

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-ELAB-0001` | error | No unique executable top can be inferred; set `project.top` or `--top`. |
| `FSIM-ELAB-001` | error | The requested top-level design unit was not found. |
| `FSIM-ELAB-002` | error | A VHDL architecture has no matching entity. |
| `FSIM-ELAB-003` | error | A qualified top-level target is malformed. |
| `FSIM-ELAB-004` | error | A qualified VHDL top does not name an architecture. |
| `FSIM-ELAB-008` | error | The elaborated design exceeds the dense process-ID space. |
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
| `FSIM-ELAB-047` | error | Assignment target and expression widths differ. |
| `FSIM-ELAB-048` | error | A VHDL `if` condition does not have scalar Boolean type. |
| `FSIM-ELAB-049` | error | Binary operands have different widths and would require implicit sizing. |
| `FSIM-ELAB-050` | error | Assignment into a two-state target would implicitly lose four- or nine-state values. |
| `FSIM-ELAB-051` | error | A VHDL assertion condition does not have scalar Boolean type. |
| `FSIM-ELAB-052` | error | A local variable has no executable packed width. |
| `FSIM-ELAB-053` | error | A local-variable declaration duplicates another declaration in the same lexical scope. |
| `FSIM-ELAB-054` | error | A local variable initializer has the wrong packed width. |
| `FSIM-ELAB-055` | error | A VHDL rejection limit exceeds its first waveform-element delay during executable lowering. |
| `FSIM-ELAB-056` | error | A local variable assignment is nonblocking. |
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
| `FSIM-ELAB-068` | error | A bit/part select does not have constant in-range bounds, a positive indexed width, or a direction compatible with its declared packed range. |
| `FSIM-ELAB-DYNINDEX-001` | error | A dynamic packed selection has a nonconcrete, null, overflowing, or otherwise unrepresentable declared range or base offset. |
| `FSIM-ELAB-DYNINDEX-002` | error | A dynamic packed selection index is not an executable VHDL `integer` value or the common signed 32-bit index representation required by this bounded implementation. |
| `FSIM-ELAB-VHSLICE-001` | error | A dynamic VHDL packed slice lacks a fixed supported width, a concrete signed 32-bit source range, matching direction, or a statically sized assignment value. |
| `FSIM-ELAB-VHSLICE-002` | error | A dynamic VHDL packed slice bound is not an executable signed 32-bit `integer`-family expression. |
| `FSIM-ELAB-VHSLICE-003` | error | A dynamic VHDL packed slice is followed by another assignment-target selection in the bounded one-dimensional composite subset. |
| `FSIM-ELAB-069` | error | A concatenation is empty or has an operand/result width that cannot be inferred or represented. |
| `FSIM-ELAB-070` | error | A dynamic VHDL packed shift or rotate count does not have the executable base `integer` subtype. |
| `FSIM-ELAB-071` | error | A sequential VHDL for-loop initial bound is not locally static. |
| `FSIM-ELAB-072` | error | A sequential VHDL for-loop final bound is not locally static. |
| `FSIM-ELAB-073` | error | A sequential VHDL for loop exceeds the bounded one-million-iteration elaboration limit. |
| `FSIM-ELAB-074` | error | A sequential for-loop body assigns its VHDL implicit constant or statically substituted SystemVerilog index. |
| `FSIM-ELAB-075` | error | A runtime Verilog/SystemVerilog repeat count is not a supported integral value. |
| `FSIM-ELAB-077` | error | A VHDL while condition is not scalar Boolean. |
| `FSIM-ELAB-078` | error | Loop-control HIR reached elaboration without an enclosing loop. |
| `FSIM-ELAB-079` | error | A VHDL wait-until condition is not scalar Boolean. |
| `FSIM-ELAB-080` | error | Targeted loop-control HIR names no enclosing loop. |
| `FSIM-ELAB-081` | error | A case statement reached elaboration with an invalid internal matching mode. |
| `FSIM-ELAB-082` | error | The bounded packed VHDL `abs` operator has a nonsigned operand. |
| `FSIM-ELAB-083` | error | A SystemVerilog `$signed` or `$unsigned` cast does not have exactly one packed argument. |
| `FSIM-ELAB-084` | error | `$isunknown` is used outside SystemVerilog or without exactly one packed argument. |
| `FSIM-ELAB-085` | error | `$bits` is used outside SystemVerilog or without one statically sized packed argument. |
| `FSIM-ELAB-086` | error | A packed array query (`$left`, `$right`, `$low`, `$high`, `$size`, or `$increment`) is used outside SystemVerilog or without one representable one-dimensional packed argument. |
| `FSIM-ELAB-087` | error | `$onehot` or `$onehot0` is used outside SystemVerilog or without exactly one packed argument. |
| `FSIM-ELAB-088` | error | `$countones` is used outside SystemVerilog or without one statically sized packed argument. |
| `FSIM-ELAB-089` | error | `$countbits` lacks a packed expression or at least one constant one-bit `0`, `1`, `X`, or `Z` control. |
| `FSIM-ELAB-090` | error | `$dimensions` or `$unpacked_dimensions` is used outside SystemVerilog or without one statically sized packed argument. |
| `FSIM-ELAB-091` | error | Bounded VHDL packed exponentiation has a dynamic or negative exponent. |
| `FSIM-ELAB-092` | error | A VHDL conditional-assignment condition is not Boolean. |
| `FSIM-ELAB-094` | error | The VHDL `'event` attribute does not name one visible signal. |
| `FSIM-ELAB-095` | error | The VHDL `'last_value` attribute does not name one visible signal. |
| `FSIM-ELAB-096` | error | The VHDL `'last_event` attribute does not name one visible signal. |
| `FSIM-ELAB-097` | error | The bounded zero-duration VHDL `'stable` attribute does not name one visible signal or supplies a duration. |
| `FSIM-ELAB-098` | error | The VHDL `'active` attribute does not name one visible signal. |
| `FSIM-ELAB-099` | error | A named-event trigger does not contain a simple event name. |
| `FSIM-ELAB-100` | error | A named-event trigger references an unknown event. |
| `FSIM-ELAB-101` | error | A named-event trigger targets an object not declared as an event. |
| `FSIM-ELAB-102` | error | A formatted-output value expression could not be lowered into SimIR. |
| `FSIM-ELAB-103` | error | A value-sensitive `$monitor` operand is not yet a direct packed-signal reference. |
| `FSIM-ELAB-104` | error | A random system function is used in an unsupported language or with an invalid argument count. |
| `FSIM-ELAB-105` | error | Procedural assignment timing-control HIR has an inconsistent control kind, delay, or event payload. |
| `FSIM-ELAB-106` | error | Procedural update metadata is inconsistent with its normalized expression or captured lvalue. |
| `FSIM-ELAB-107` | error | A bounded fork appears inside a callable whose shared call frame cannot safely outlive the caller. |
| `FSIM-ELAB-108` | error | A postponed `$strobe` operand is not yet a direct packed-signal reference. |
| `FSIM-ELAB-DRV-001` | error | An unresolved variable has multiple process drivers. |
| `FSIM-ELAB-HIER-001` | error | Duplicate elaborated instance path. |
| `FSIM-ELAB-HIER-002` | error | Recursive instantiation was detected. |
| `FSIM-ELAB-INTEGER-001` | error | A specialization-dependent VHDL integer subtype constraint cannot be evaluated. |
| `FSIM-ELAB-INTEGER-002` | error | A VHDL integer subtype constraint is null, outside its named base subtype, or outside fsim's portable signed 32-bit representation. |
| `FSIM-ELAB-INTEGER-003` | error | A locally static VHDL integer value lies outside its assignment target's concrete subtype range. |
| `FSIM-ELAB-INTEGER-004` | error | A VHDL integer-family target is assigned a packed or otherwise noninteger expression without explicit conversion. |
| `FSIM-ELAB-GEN-001` | error | A conditional-generate expression cannot be evaluated for its specialization. |
| `FSIM-ELAB-GEN-002` | error | A loop-generate initial value cannot be evaluated for its specialization. |
| `FSIM-ELAB-GEN-003` | error | A loop-generate continuation condition cannot be evaluated. |
| `FSIM-ELAB-GEN-004` | error | A loop generate exceeds the bounded one-million-iteration elaboration limit. |
| `FSIM-ELAB-GEN-005` | error | A loop-generate iteration expression cannot be evaluated. |
| `FSIM-ELAB-GEN-006` | error | A loop-generate iteration does not advance its variable. |
| `FSIM-ELAB-GEN-007` | error | A nested loop-generate variable shadows an enclosing constant in the bounded executable slice. |
| `FSIM-ELAB-GEN-008` | error | A selection-generate selector cannot be evaluated in its locally static scalar or enumeration domain. |
| `FSIM-ELAB-GEN-009` | error | A selection-generate choice or range bound is not locally static or does not belong to the selector domain. |
| `FSIM-ELAB-GEN-010` | error | Selection-generate choices or ranges overlap, or the alternatives contain duplicate defaults. |
| `FSIM-ELAB-GEN-011` | error | A generated constant or parameter cannot be evaluated in its declaration-order environment. |
| `FSIM-ELAB-GEN-012` | error | A generated constant/parameter violates a bounded scalar subtype or exceeds the 64-bit integral width. |
| `FSIM-ELAB-GEN-013` | error | A VHDL block guard expression has a non-Boolean type. |
| `FSIM-ELAB-VHBLOCK-001` | error | A VHDL block generic map is missing, excessive, duplicated, unknown, misordered, or selects an unavailable default. |
| `FSIM-ELAB-VHBLOCK-002` | error | A VHDL block port map is missing, excessive, duplicated, unknown, misordered, or uses an illegal actual for the formal mode. |
| `FSIM-ELAB-VHBLOCK-003` | error | A VHDL object or block-port alias names an unknown or profile-incompatible signal target. |
| `FSIM-ELAB-PKG-001` | error | A bounded VHDL package import is not `library.package.all` or `library.package.constant`. |
| `FSIM-ELAB-PKG-002` | error | A project VHDL package named by a use clause was not found in the selected library. |
| `FSIM-ELAB-PKG-003` | error | A selected package constant or type named by a use clause does not exist. |
| `FSIM-ELAB-PKG-004` | error | The same bare VHDL constant name is directly visible from multiple imported packages. |
| `FSIM-ELAB-PKG-005` | error | A package constant default cannot be evaluated in declaration order. |
| `FSIM-ELAB-PKG-006` | error | A package constant value violates its bounded scalar subtype. |
| `FSIM-ELAB-PKG-007` | error | Project-package use visibility contains a dependency cycle. |
| `FSIM-ELAB-PKG-008` | error | A selected package constant is not `package.constant` or `library.package.constant`. |
| `FSIM-ELAB-PKG-009` | error | A package named by a selected constant expression was not found in the requested library. |
| `FSIM-ELAB-PKG-010` | error | A selected constant does not exist in the resolved package. |
| `FSIM-ELAB-CTX-001` | error | A bounded context reference is not `library.context`. |
| `FSIM-ELAB-CTX-002` | error | A project context referenced by a library unit or another context was not found. |
| `FSIM-ELAB-CTX-003` | error | Reusable VHDL context visibility contains a dependency cycle. |
| `FSIM-ELAB-SVPKG-001` | error | A SystemVerilog package named by an import or scoped item was not found in the owning library. |
| `FSIM-ELAB-SVPKG-002` | error | A selected or scoped item does not exist in the resolved SystemVerilog package. |
| `FSIM-ELAB-SVPKG-003` | error | The same direct constant name is imported from multiple SystemVerilog packages. |
| `FSIM-ELAB-SVPKG-004` | error | Recursive SystemVerilog package imports contain a visibility cycle. |
| `FSIM-ELAB-SVPKG-005` | error | A package-scoped item is not exactly `package::name`. |
| `FSIM-ELAB-SVPKG-006` | error | A SystemVerilog package constant default cannot be evaluated in declaration order. |
| `FSIM-ELAB-SVPKG-007` | error | A package export has invalid wildcard shape or is not backed by a matching import. |
| `FSIM-ELAB-SVPKG-008` | error | A selective package export does not select an imported declaration. |
| `FSIM-ELAB-SVTYPE-001` | error | A SystemVerilog user-defined type is not visible in the unit where it is used. |
| `FSIM-ELAB-SVTYPE-002` | error | The same direct type name is imported from multiple SystemVerilog packages. |
| `FSIM-ELAB-SVTYPE-003` | error | Bounded SystemVerilog typedef aliases contain a cycle. |
| `FSIM-ELAB-SVTYPE-004` | error | An aggregate assignment does not use the same nominal type, a matching explicit cast, or a contextual pattern. |
| `FSIM-ELAB-SVTYPE-005` | error | SystemVerilog aggregate equality compares values with different or missing nominal aggregate types. |
| `FSIM-ELAB-SVLOOP-001` | error | A runtime procedural for-loop inline variable shadows an active local. |
| `FSIM-ELAB-SVLOOP-002` | error | A runtime procedural for-loop condition is not executable as a packed truth value. |
| `FSIM-ELAB-SVEVENT-001` | error | A packed event expression has no readable signal dependencies. |
| `FSIM-ELAB-SVEVENT-002` | error | Packed event-expression HIR is mixed with another event or timeout. |
| `FSIM-ELAB-SVEVENT-003` | error | A packed event expression does not have an executable width from 1 through 64 bits. |
| `FSIM-ELAB-SVEVENT-004` | error | A repeated event-control count is not an executable integral value. |
| `FSIM-ELAB-SVDELAY-001` | error | A SystemVerilog delay expression is not a known nonnegative locally constant integral value after specialization. |
| `FSIM-ELAB-SVDELAY-002` | error | A specialized SystemVerilog delay expression overflows 64-bit simulation time after time-unit normalization. |
| `FSIM-ELAB-SVDELAY-003` | error | Combined continuous-assignment and net-declaration transition delays overflow 64-bit simulation time. |
| `FSIM-ELAB-SVIFACE-001` | error | An interface port actual is not a whole scalar or statically indexed interface instance. |
| `FSIM-ELAB-SVIFACE-002` | error | An interface port actual does not name an already elaborated interface instance. |
| `FSIM-ELAB-SVIFACE-003` | error | An interface port actual has the wrong interface type. |
| `FSIM-ELAB-SVIFACE-004` | error | An interface port selects a modport that its interface type does not declare. |
| `FSIM-ELAB-SVIFACE-005` | error | A retained modport member has no elaborated interface signal. |
| `FSIM-ELAB-SVFUNC-001` | error | The visible bounded function set exceeds the representable SimIR call-stack capacity. |
| `FSIM-ELAB-SVFUNC-002` | error | More than one bounded function has the same visible name. |
| `FSIM-ELAB-SVFUNC-003` | error | A bounded function call has the wrong number of arguments. |
| `FSIM-ELAB-SVFUNC-004` | error | A bounded function return or formal type does not specialize to an executable width from 1 through 64 bits. |
| `FSIM-ELAB-SVFUNC-005` | error | A return statement is outside an executable function or lacks a value during lowering. |
| `FSIM-ELAB-SVFUNC-006` | error | Bounded runtime functions contain a direct or indirect recursive call cycle. |
| `FSIM-ELAB-SVFUNC-007` | error | The same bare function name is directly visible from multiple imported SystemVerilog packages. |
| `FSIM-ELAB-SVFUNC-008` | error | A bounded SystemVerilog function result or its whole-container destination has an incompatible container kind, element profile, queue bound, or associative index profile. |
| `FSIM-ELAB-SVFUNC-009` | error | A bounded SystemVerilog function container argument has an incompatible kind, element profile, queue bound, or associative index profile. |
| `FSIM-ELAB-SVFUNC-010` | error | Function actual association metadata is inconsistent, positional ordering is illegal, a name is unknown or duplicated, or a required actual/default is missing. |
| `FSIM-ELAB-SVFUNC-011` | error | Malformed HIR presents a nonintegral writable function formal to the bounded execution path. |
| `FSIM-ELAB-SVFUNC-012` | error | A bounded `ref` function actual is not a direct caller-local variable or the function is not automatic. |
| `FSIM-ELAB-SVFUNC-013` | error | A static or implicit-lifetime function local is not a bounded packed integral value. |
| `FSIM-ELAB-VHFUNC-001` | error | An interface-function generic has no retained profile in HIR. |
| `FSIM-ELAB-VHFUNC-002` | error | An interface-function association or selected actual is not same-language VHDL. |
| `FSIM-ELAB-VHFUNC-003` | error | An interface-function actual is not a simple visible function name. |
| `FSIM-ELAB-VHFUNC-004` | error | A required interface-function generic has no actual or default. |
| `FSIM-ELAB-VHFUNC-005` | error | A named function actual is invisible or has no conforming supported profile. |
| `FSIM-ELAB-VHFUNC-006` | error | A named or box-default function actual is ambiguous among conforming visible functions. |
| `FSIM-ELAB-VHFUNC-007` | error | A selected function actual has no executable body. |
| `FSIM-ELAB-VHFUNC-008` | error | A selected function actual is impure in the bounded interface-function subset. |
| `FSIM-ELAB-VHFUNC-009` | error | An interface-function binding conflicts with a child-local function name. |
| `FSIM-ELAB-VHCONV-002` | error | A visible VHDL conversion target has no bounded executable width from 1 through 64 bits. |
| `FSIM-ELAB-VHCONV-003` | error | A VHDL conversion operand is not a supported closely related type or would change packed width or state domain. |
| `FSIM-ELAB-VHCONV-004` | error | A VHDL conversion result is incompatible with its contextual type. |
| `FSIM-ELAB-VHOVER-001` | error | A VHDL function call is ambiguous among the visible overloads after result and actual-profile filtering. |
| `FSIM-ELAB-VHOVER-002` | error | A VHDL function call matches no visible overload after result and actual-profile filtering. |
| `FSIM-ELAB-VHOVER-003` | error | Two visible VHDL function declarations have the same callable profile. |
| `FSIM-ELAB-VHOVER-004` | error | A VHDL procedure call is ambiguous among the visible overloads after actual-profile filtering. |
| `FSIM-ELAB-VHOVER-005` | error | A VHDL procedure call matches no visible overload after actual-profile filtering. |
| `FSIM-ELAB-VHOVER-006` | error | Two visible VHDL procedure declarations have the same callable profile. |
| `FSIM-ELAB-VHQUAL-001` | error | A VHDL qualified expression names a type mark that is not visible. |
| `FSIM-ELAB-VHQUAL-002` | error | A VHDL qualification target has no bounded executable width from 1 through 64 bits. |
| `FSIM-ELAB-VHQUAL-003` | error | A VHDL qualified expression operand does not have the target base type, exact bounded shape, width, nominal identity, or state domain. |
| `FSIM-ELAB-VHQUAL-004` | error | A VHDL qualified-expression result is incompatible with its contextual type. |
| `FSIM-ELAB-VHLEGAL-001` | error | A VHDL package function body does not conform to any same-designator declaration. |
| `FSIM-ELAB-VHLEGAL-002` | error | A VHDL package function declaration has no conforming body. |
| `FSIM-ELAB-VHLEGAL-003` | error | A VHDL package procedure body does not conform to any same-designator declaration. |
| `FSIM-ELAB-VHLEGAL-004` | error | A VHDL package procedure declaration has no conforming body. |
| `FSIM-ELAB-VHLEGAL-005` | error | A pure VHDL function reads a non-formal signal. |
| `FSIM-ELAB-VHLEGAL-006` | error | A pure VHDL function calls a procedure. |
| `FSIM-ELAB-VHLEGAL-007` | error | A VHDL function formal default does not match its subtype. |
| `FSIM-ELAB-VHLEGAL-008` | error | A VHDL procedure formal default does not match its subtype. |
| `FSIM-ELAB-VHLEGAL-009` | error | A VHDL function contains a wait or calls a suspending procedure. |
| `FSIM-ELAB-VHREPORT-001` | error | A VHDL report expression does not have string type. |
| `FSIM-ELAB-VHREPORT-002` | error | A VHDL severity expression does not have severity_level type. |
| `FSIM-ELAB-VHFILE-001` | error | A VHDL scope declares the same file object more than once. |
| `FSIM-ELAB-VHFILE-002` | error | A VHDL file object has no visible retained file type. |
| `FSIM-ELAB-VHFILE-003` | error | A VHDL file declaration logical name is not a string expression. |
| `FSIM-ELAB-VHFILE-004` | error | A VHDL file open kind is not the supported static `read_mode`, `write_mode`, or `append_mode` value. |
| `FSIM-ELAB-VHFILE-005` | error | A VHDL file operation does not name a visible whole file object. |
| `FSIM-ELAB-VHFILE-006` | error | `file_close` does not have exactly one file-object actual. |
| `FSIM-ELAB-VHFILE-007` | error | `file_open` has an invalid actual count or non-string logical name. |
| `FSIM-ELAB-VHFILE-008` | error | Status-form `file_open` does not have a writable `file_open_status` actual. |
| `FSIM-ELAB-VHFILE-009` | error | `endfile` does not have exactly one visible whole file-object actual. |
| `FSIM-ELAB-VHFILE-010` | error | Direct VHDL file `read` or `write` does not have exactly one value actual. |
| `FSIM-ELAB-VHFILE-011` | error | Direct VHDL file I/O uses an element type outside the bounded integer subset. |
| `FSIM-ELAB-VHFILE-012` | error | Direct VHDL file `read` does not target a writable integer variable. |
| `FSIM-ELAB-VHTEXTIO-001` | error | `readline` or `writeline` does not have one text-file and one writable `line` actual. |
| `FSIM-ELAB-VHTEXTIO-002` | error | A selected TextIO `read` or `write` line actual is not writable. |
| `FSIM-ELAB-VHTEXTIO-003` | error | A TextIO `read` or `write` profile has an invalid actual count or no value actual. |
| `FSIM-ELAB-VHTEXTIO-004` | error | A TextIO `read` value is not a writable bounded scalar. |
| `FSIM-ELAB-VHTEXTIO-005` | error | A TextIO `read` value type is outside the bounded integer, Boolean, and bit profiles. |
| `FSIM-ELAB-VHTEXTIO-006` | error | A TextIO `read` `good` actual is not a writable Boolean. |
| `FSIM-ELAB-VHTEXTIO-007` | error | A TextIO `write` justification is not the static `left` or `right` value. |
| `FSIM-ELAB-VHTEXTIO-008` | error | A TextIO `write` field is not a static value in the bounded range 0 through 4096. |
| `FSIM-ELAB-VHTEXTIO-009` | error | A TextIO `write` value type is outside the bounded integer, Boolean, bit, and string profiles. |
| `FSIM-ELAB-VHTIME-001` | error | A VHDL delay or physical-time literal does not have a locally static integral magnitude. |
| `FSIM-ELAB-VHTIME-002` | error | A VHDL physical-time value overflows the bounded signed 64-bit tick representation. |
| `FSIM-ELAB-VHTIME-003` | error | A VHDL physical-time literal is not exactly representable at the selected project resolution. |
| `FSIM-ELAB-VHNAME-001` | error | A VHDL function or type mark is not visible in an expression context. |
| `FSIM-ELAB-VHRESOLVE-001` | error | A VHDL resolution indication names no visible function with an executable body. |
| `FSIM-ELAB-VHRESOLVE-002` | error | A VHDL resolution function does not have the required pure array-input/base-result profile. |
| `FSIM-ELAB-VHRESOLVE-003` | error | A VHDL resolution function is ambiguous or conflicts with another visible body. |
| `FSIM-ELAB-VHRESOLVE-004` | error | A VHDL resolution function body is outside the bounded OR/AND reduction subset. |
| `FSIM-ELAB-VHSTATIC-001` | error | A VHDL scalar or array type attribute or integer-subtype conversion is not locally static. |
| `FSIM-ELAB-VHSTATIC-002` | error | A locally static VHDL type attribute or integer-subtype conversion is outside the representable or declared range. |
| `FSIM-ELAB-VHPROC-001` | error | An interface-procedure generic has no retained profile in HIR. |
| `FSIM-ELAB-VHPROC-002` | error | An interface-procedure association or selected actual is not same-language VHDL. |
| `FSIM-ELAB-VHPROC-003` | error | An interface-procedure actual is not a simple visible procedure name. |
| `FSIM-ELAB-VHPROC-004` | error | A required interface-procedure generic has no actual or default. |
| `FSIM-ELAB-VHPROC-005` | error | A named procedure actual is invisible or has no conforming class, mode, and type profile. |
| `FSIM-ELAB-VHPROC-006` | error | A named or box-default procedure actual is ambiguous among conforming visible procedures. |
| `FSIM-ELAB-VHPROC-007` | error | A selected procedure actual has no executable body. |
| `FSIM-ELAB-VHPROC-008` | error | An interface-procedure actual names a function rather than a procedure. |
| `FSIM-ELAB-VHPROC-009` | error | A selected procedure actual contains timing or a non-variable update. |
| `FSIM-ELAB-VHPROC-010` | error | A recursive VHDL procedure call graph is unsupported. |
| `FSIM-ELAB-VHPROC-011` | error | An interface-procedure binding conflicts with a child-local subprogram name. |
| `FSIM-ELAB-VHPROC-012` | error | A generated or scoped procedure actual is outside the bounded interface-procedure subset. |
| `FSIM-ELAB-VHPROC-014` | error | A VHDL procedure call names no visible procedure. |
| `FSIM-ELAB-VHPROC-015` | error | A named VHDL procedure-call association is unknown or duplicated. |
| `FSIM-ELAB-VHPROC-016` | error | A positional VHDL procedure actual follows a named actual during lowering. |
| `FSIM-ELAB-VHPROC-017` | error | A VHDL procedure call has too many or too few actuals. |
| `FSIM-ELAB-VHPROC-018` | error | A variable-class, output, or inout procedure formal receives a non-writable actual. |
| `FSIM-ELAB-VHPROC-019` | error | The visible VHDL procedure set exceeds the SimIR call-stack capacity. |
| `FSIM-ELAB-VHPROC-020` | error | A VHDL procedure formal has no executable width in the bounded 1–64-bit representation. |
| `FSIM-ELAB-VHPROC-021` | error | A VHDL procedure return is outside a procedure or carries a value. |
| `FSIM-ELAB-VHPKG-001` | error | An interface-package generic has no retained profile, or a nested interface-package formal appears in a bounded generic package template. |
| `FSIM-ELAB-VHPKG-002` | error | An interface-package association crosses a non-VHDL language boundary. |
| `FSIM-ELAB-VHPKG-003` | error | An interface-package actual is not a simple visible package-instance name. |
| `FSIM-ELAB-VHPKG-004` | error | A required interface-package generic has no actual package instance. |
| `FSIM-ELAB-VHPKG-005` | error | A named package-instance actual is not directly visible. |
| `FSIM-ELAB-VHPKG-006` | error | An interface-package actual resolves to a value, type, function, or procedure rather than a package instance. |
| `FSIM-ELAB-VHPKG-007` | error | A package-instance actual specializes a different generic package template. |
| `FSIM-ELAB-VHPKG-008` | error | A package-instance actual does not conform to the interface package's explicit or defaulted generic map. |
| `FSIM-ELAB-VHPKG-009` | error | A generic package template name is malformed or not found in the requested library. |
| `FSIM-ELAB-VHPKG-010` | error | A selected bounded package instance has an incomplete required function or procedure body. |
| `FSIM-ELAB-VHPKG-011` | error | A generated or scoped package actual is outside the bounded interface-package subset. |
| `FSIM-ELAB-VHPKG-012` | error | A generic package template or local package-instance name is ambiguous. |
| `FSIM-ELAB-VHPKG-013` | error | A local package instantiation selects a nongeneric package declaration. |
| `FSIM-ELAB-VHPKG-014` | error | A generic package template is passed without first creating a package instance. |
| `FSIM-ELAB-VHGSUB-001` | error | A generic subprogram template is missing or not directly visible before its instantiation. |
| `FSIM-ELAB-VHGSUB-002` | error | A generic subprogram template is ambiguous locally or across directly visible packages. |
| `FSIM-ELAB-VHGSUB-003` | error | A generic subprogram instantiation selects the wrong subprogram kind or a nongeneric subprogram. |
| `FSIM-ELAB-VHGSUB-004` | error | A selected generic subprogram template has no conforming executable body. |
| `FSIM-ELAB-VHGSUB-006` | error | A generic subprogram template is called before being instantiated. |
| `FSIM-ELAB-VHGSUB-007` | error | A bounded generic subprogram template contains a recursive call. |
| `FSIM-ELAB-VHGSUB-008` | error | A materialized generic subprogram instance conflicts with another callable subprogram. |
| `FSIM-ELAB-VHGSUB-009` | error | A selected or scoped generic subprogram template is outside the bounded directly visible subset. |
| `FSIM-ELAB-VHGSUB-011` | error | A generic subprogram template contains an unsupported interface-package formal. |
| `FSIM-ELAB-VHGSUB-012` | error | Generic subprogram specialization failed to retain its selected template body. |
| `FSIM-ELAB-VHGSUB-013` | error | A generic subprogram declaration and body do not have conforming generic lists and callable profiles. |
| `FSIM-ELAB-VHCONFIG-001` | error | A VHDL configuration design unit has no retained block-configuration payload. |
| `FSIM-ELAB-VHCONFIG-002` | error | A configuration declaration's target entity is missing or ambiguous in its library. |
| `FSIM-ELAB-VHCONFIG-003` | error | A configuration declaration selects a missing architecture of its target entity. |
| `FSIM-ELAB-VHCONFIG-004` | error | A configuration declaration selects an ambiguous architecture. |
| `FSIM-ELAB-VHCONFIG-005` | error | A configuration rule names a component with no component-style instances in the configured region. |
| `FSIM-ELAB-VHCONFIG-006` | error | A configuration label does not select an instance of the stated component. |
| `FSIM-ELAB-VHCONFIG-007` | error | Configuration label, `all`, or `others` rules overlap or occur more than once for one component. |
| `FSIM-ELAB-VHCONFIG-008` | error | A configuration binding has a malformed entity aspect or selects a missing VHDL entity/architecture. |
| `FSIM-ELAB-VHCONFIG-009` | error | A configuration binding selects an ambiguous VHDL entity/architecture. |
| `FSIM-ELAB-VHCONFIG-010` | error | A nested block/generate configuration has a non-static index or does not select an elaborated occurrence. |
| `FSIM-ELAB-VHCONFIG-011` | error | A positional component map remained after component-formal normalization and cannot be composed through a configuration binding map. |
| `FSIM-ELAB-VHCONFIG-012` | error | A configuration port map is not a supported named target-port to component-port mapping. |
| `FSIM-ELAB-VHCONFIG-013` | error | A configuration binding has a malformed configuration aspect or selects a missing configuration declaration. |
| `FSIM-ELAB-VHCONFIG-014` | error | A configuration binding selects an ambiguous configuration declaration. |
| `FSIM-ELAB-VHCONFIG-015` | error | A nested block/generate scope is configured more than once at the same level. |
| `FSIM-ELAB-VHCOMP-001` | error | A component-style instance has no visible bounded component declaration. |
| `FSIM-ELAB-VHCOMP-002` | error | A component-style instance ambiguously matches multiple equally visible component declarations. |
| `FSIM-ELAB-VHCOMP-003` | error | Default component binding finds no same-library VHDL entity or architecture. |
| `FSIM-ELAB-VHCOMP-005` | error | A default or configured component target has a missing or ambiguous entity interface. |
| `FSIM-ELAB-VHCOMP-006` | error | A component generic profile is incompatible with its bound entity generic profile. |
| `FSIM-ELAB-VHCOMP-007` | error | A component port profile, mode, type, or width is incompatible with its bound entity port profile. |
| `FSIM-ELAB-VHCOMP-008` | error | A component generic association is unknown, duplicated, out of order, excessive, or omits a required formal. |
| `FSIM-ELAB-VHCOMP-009` | error | A component port association is unknown, duplicated, out of order, excessive, omits a required formal, or opens a required input without a default. |
| `FSIM-ELAB-VHCOMP-010` | error | A configuration binding map names an unknown component or entity formal. |
| `FSIM-ELAB-VHCOMP-011` | error | Default component binding would cross languages and requires an explicit manifest binding. |
| `FSIM-ELAB-VHCOMP-012` | error | No equally visible component overload matches the instance associations, modes, types, or dependent widths. |
| `FSIM-ELAB-VHCOMP-013` | error | A selected component input default is dynamic, malformed, or incompatible with the bounded scalar/vector/enumeration/record/array port type. |
| `FSIM-ELAB-VHCOMP-014` | error | A component generic association selects `<>`, but the selected component formal has no usable default. |
| `FSIM-ELAB-VHCOMP-015` | error | A selected component non-value generic actual cannot be forwarded into the bound entity profile. |
| `FSIM-ELAB-SVTASK-001` | error | A bounded task call names no visible task. |
| `FSIM-ELAB-SVTASK-003` | error | The visible bounded task set exceeds the representable SimIR call-stack capacity. |
| `FSIM-ELAB-SVTASK-004` | error | More than one bounded task has the same visible name. |
| `FSIM-ELAB-SVTASK-005` | error | A bounded task call has the wrong number of arguments. |
| `FSIM-ELAB-SVTASK-006` | error | A bounded task formal type does not specialize to an executable width from 1 through 64 bits. |
| `FSIM-ELAB-SVTASK-007` | error | A task return statement is outside an executable task during lowering. |
| `FSIM-ELAB-SVTASK-008` | error | Bounded runtime tasks contain a direct or indirect recursive call cycle. |
| `FSIM-ELAB-SVTASK-009` | error | The same bare task name is directly visible from multiple imported SystemVerilog packages. |
| `FSIM-ELAB-SVTASK-010` | error | A suspending bounded task is called from `final`, `always_comb`, or `always_latch`. |
| `FSIM-ELAB-SVTASK-011` | error | A bounded SystemVerilog task container input or inout actual has an incompatible kind, element profile, queue bound, or associative index profile. |
| `FSIM-ELAB-SVTASK-012` | error | Task actual association metadata is inconsistent, positional ordering is illegal, a name is unknown or duplicated, or a required actual/default is missing. |
| `FSIM-ELAB-SVTASK-013` | error | A bounded `ref` task call is not automatic and nonsuspending or its actual is not a direct caller-local variable. |
| `FSIM-ELAB-SVTASK-014` | error | A static or implicit-lifetime task may suspend directly or transitively. |
| `FSIM-ELAB-SVTASK-015` | error | A static or implicit-lifetime task local is not a bounded packed integral value. |
| `FSIM-ELAB-VHTYPE-001` | error | A bounded VHDL named type is not visible in the design unit where it is used. |
| `FSIM-ELAB-VHTYPE-002` | error | Bounded VHDL named type aliases contain a cycle. |
| `FSIM-ELAB-VHTYPE-003` | error | The same VHDL type name is directly visible from multiple packages. |
| `FSIM-ELAB-VHTYPE-004` | error | A selected VHDL package type name is malformed or does not exist. |
| `FSIM-ELAB-VHACCESS-001` | error | VHDL access type declarations contain a recursive designated-subtype cycle. |
| `FSIM-ELAB-VHACCESS-002` | error | A VHDL access declaration does not resolve to exactly one designated subtype. |
| `FSIM-ELAB-VHACCESS-003` | error | A bounded VHDL access type designates a protected type. |
| `FSIM-ELAB-VHACCESS-004` | error | A VHDL access type has an invalid bounded handle or object-capacity representation. |
| `FSIM-ELAB-VHACCESS-005` | error | A VHDL access operation lacks one resolved designated subtype. |
| `FSIM-ELAB-VHACCESS-006` | error | A VHDL allocator's designated subtype lacks a concrete nonempty bounded packed representation. |
| `FSIM-ELAB-VHACCESS-007` | error | A VHDL allocator designates a nine-state object outside the bounded access-object state subset. |
| `FSIM-ELAB-VHACCESS-008` | error | The VHDL `null` access value lacks an access-type context. |
| `FSIM-ELAB-VHACCESS-009` | error | A VHDL access context has an inconsistent bounded handle width. |
| `FSIM-ELAB-VHACCESS-010` | error | A VHDL allocator lacks a resolved access-type context. |
| `FSIM-ELAB-VHACCESS-011` | error | An allocator subtype differs from its access type's designated subtype. |
| `FSIM-ELAB-VHACCESS-012` | error | A qualified VHDL allocator does not supply exactly one initial value. |
| `FSIM-ELAB-VHACCESS-013` | error | A VHDL allocator initializer has the wrong designated-object width. |
| `FSIM-ELAB-VHACCESS-014` | error | A VHDL dereference read or target lacks one access-typed prefix. |
| `FSIM-ELAB-VHACCESS-015` | error | A dereferenced access object is assigned with timing or non-variable semantics. |
| `FSIM-ELAB-VHACCESS-016` | error | A dereferenced assignment has the wrong designated-object width. |
| `FSIM-ELAB-VHACCESS-017` | error | An operator other than equality or inequality is applied to VHDL access values. |
| `FSIM-ELAB-VHACCESS-018` | error | VHDL access equality is given a nonaccess, nonnull operand. |
| `FSIM-ELAB-VHACCESS-019` | error | VHDL access equality compares distinct nominal access types. |
| `FSIM-ELAB-VHACCESS-020` | error | VHDL access assignment is not a same-nominal value or `null`. |
| `FSIM-ELAB-VHACCESS-021` | error | A nonnull process-local VHDL access handle escapes through a signal. |
| `FSIM-ELAB-VHACCESS-022` | error | Explicit access deallocation is requested for a bounded simulation-lifetime object. |
| `FSIM-ELAB-VHPHYSICAL-001` | error | A VHDL physical type lacks its range or primary unit. |
| `FSIM-ELAB-VHPHYSICAL-002` | error | A physical range is nonstatic or outside signed 32-bit primary-unit ticks. |
| `FSIM-ELAB-VHPHYSICAL-003` | error | A physical primary unit incorrectly carries a secondary-unit scale. |
| `FSIM-ELAB-VHPHYSICAL-004` | error | A secondary physical unit has a nonpositive, nonstatic, forward, or overflowing scale. |
| `FSIM-ELAB-VHPHYSICAL-005` | error | A bounded physical value does not use the signed 32-bit runtime representation. |
| `FSIM-ELAB-VHPHYSICAL-006` | error | A physical literal uses an unknown unit, nonstatic magnitude, overflow, or out-of-range value. |
| `FSIM-ELAB-VHPHYSICAL-007` | error | An unsupported operator is applied to a bounded physical value. |
| `FSIM-ELAB-VHPHYSICAL-008` | error | A physical expression combines distinct nominal physical types. |
| `FSIM-ELAB-VHPHYSICAL-009` | error | Assignment mixes distinct nominal physical types. |
| `FSIM-ELAB-VHPHYSICAL-010` | error | A physical target receives an untyped nonphysical value without explicit conversion. |
| `FSIM-ELAB-VHPROTECTED-001` | error | A protected body has no visible protected type declaration. |
| `FSIM-ELAB-VHPROTECTED-002` | error | A public protected function has no conforming body. |
| `FSIM-ELAB-VHPROTECTED-003` | error | A protected function body has no conforming public profile. |
| `FSIM-ELAB-VHPROTECTED-004` | error | A public protected procedure has no conforming body. |
| `FSIM-ELAB-VHPROTECTED-005` | error | A protected procedure body has no conforming public profile. |
| `FSIM-ELAB-VHPROTECTED-006` | error | A protected type declaration has no body. |
| `FSIM-ELAB-VHPROTECTED-007` | error | A protected private variable lacks a bounded supported scalar or packed type. |
| `FSIM-ELAB-VHPROTECTED-008` | error | A VHDL shared variable does not have a protected type. |
| `FSIM-ELAB-VHPROTECTED-009` | error | A shared protected object lacks one conforming protected body. |
| `FSIM-ELAB-VHPROTECTED-010` | error | A shared protected object incorrectly carries an object initializer. |
| `FSIM-ELAB-VHPROTECTED-011` | error | A protected private initializer is nonstatic or incompatible with its member. |
| `FSIM-ELAB-VHPROTECTED-012` | error | A protected function call matches no public profile. |
| `FSIM-ELAB-VHPROTECTED-013` | error | A protected function call ambiguously matches multiple public profiles. |
| `FSIM-ELAB-VHPROTECTED-014` | error | A protected method re-enters protected execution. |
| `FSIM-ELAB-VHPROTECTED-015` | error | A bounded protected function is not a single direct value return. |
| `FSIM-ELAB-VHPROTECTED-016` | error | A protected function result is incompatible with its expression context. |
| `FSIM-ELAB-VHPROTECTED-017` | error | Protected private member storage is unavailable or has an invalid bounded width. |
| `FSIM-ELAB-VHPROTECTED-018` | error | A protected procedure call matches no supported public input profile. |
| `FSIM-ELAB-VHPROTECTED-019` | error | A protected procedure call ambiguously matches multiple public profiles. |
| `FSIM-ELAB-VHPROTECTED-020` | error | A protected method attempts to suspend. |
| `FSIM-ELAB-VHPROTECTED-021` | error | A protected method makes a nested procedure call outside the bounded non-reentrant policy. |
| `FSIM-ELAB-VHAGG-001` | error | A VHDL aggregate appears without a supported contextual record or array target type. |
| `FSIM-ELAB-VHAGG-002` | error | A contextual record layout or aggregate-association HIR payload is internally inconsistent. |
| `FSIM-ELAB-VHAGG-003` | error | A named aggregate association does not name an element of the contextual record type. |
| `FSIM-ELAB-VHAGG-004` | error | A record aggregate assigns an element more than once, has too many positional associations, or repeats `others`. |
| `FSIM-ELAB-VHAGG-005` | error | A record aggregate omits an element without supplying `others`. |
| `FSIM-ELAB-VHAGG-006` | error | A record aggregate element value does not have the element's exact packed width. |
| `FSIM-ELAB-VHAGG-007` | error | A record aggregate would implicitly lose four- or nine-state information in a two-state element. |
| `FSIM-ELAB-VHAGG-008` | error | A record aggregate choice is empty, names `others` with another choice, or uses a discrete/range form instead of element names. |
| `FSIM-ELAB-VHAGG-009` | error | A record aggregate element value has an incompatible contextual subtype or state domain. |
| `FSIM-ELAB-VHRECORD-001` | error | A VHDL record element does not resolve to a concrete bounded packed scalar, vector, enumeration, array, or nested-record layout. |
| `FSIM-ELAB-VHRECORD-002` | error | A VHDL record element or total recursive record layout overflows the bounded packed representation. |
| `FSIM-ELAB-VHCOMPOP-001` | error | VHDL composite comparison operands do not share one nominal record or array base and compatible element profile, or lack bounded executable widths. |
| `FSIM-ELAB-VHCOMPOP-002` | error | A VHDL composite assignment source has an incompatible nominal base, element profile, rank, or dimension length. |
| `FSIM-ELAB-VHCOMPOP-003` | error | A VHDL concatenation lacks a bounded one-dimensional array context or has incompatible operands, element profiles, state domains, or result length. |
| `FSIM-ELAB-VHCOMPOP-004` | error | A record member in an interleaved VHDL array/record selection chain is unknown or has no bounded executable read or assignment layout. |
| `FSIM-ELAB-VHSUBTYPE-001` | error | A scalar `range` constraint is applied to a resolved noninteger base subtype. |
| `FSIM-ELAB-VHSUBTYPE-002` | error | A derived integer subtype constraint lies outside its resolved base subtype. |
| `FSIM-ELAB-VHSUBTYPE-003` | error | A packed index constraint is applied to a scalar, record, or otherwise nonarray base subtype. |
| `FSIM-ELAB-VHSUBTYPE-004` | error | A constrained packed-array subtype is constrained again. |
| `FSIM-ELAB-VHARRAY-001` | error | A VHDL array element subtype is missing, indefinite, zero-width, integer, string, unknown, or otherwise outside the bounded packed runtime. |
| `FSIM-ELAB-VHARRAY-002` | error | A null constraint on a legacy built-in VHDL vector cannot yet be represented by its one-dimensional packed path. |
| `FSIM-ELAB-VHARRAY-003` | error | A VHDL array constraint lies outside its `integer`, `natural`, or `positive` index subtype. |
| `FSIM-ELAB-VHARRAY-004` | error | A VHDL array constraint width overflows the packed runtime representation. |
| `FSIM-ELAB-VHARRAY-005` | error | A VHDL array object uses an unconstrained or otherwise nonconcrete array subtype. |
| `FSIM-ELAB-VHARRAY-006` | error | Assignment or comparison mixes values from different nominal VHDL array types. |
| `FSIM-ELAB-VHARRAY-007` | error | An operator other than equality, inequality, or matching equality is applied to a VHDL array value in the current bounded semantic path. |
| `FSIM-ELAB-VHARRAY-008` | error | The number of constraints on a VHDL array subtype indication does not match the base array rank. |
| `FSIM-ELAB-VHARRAYAGG-002` | error | Contextual VHDL array layout or aggregate-choice HIR metadata is inconsistent with the aggregate value. |
| `FSIM-ELAB-VHARRAYAGG-003` | error | A VHDL array aggregate choice is nonstatic or outside the contextual index range. |
| `FSIM-ELAB-VHARRAYAGG-004` | error | A VHDL array aggregate covers an index more than once, has too many positional values, or repeats `others`. |
| `FSIM-ELAB-VHARRAYAGG-005` | error | A VHDL array aggregate leaves a contextual index uncovered without supplying `others`. |
| `FSIM-ELAB-VHARRAYAGG-006` | error | A VHDL array aggregate element has the wrong packed width. |
| `FSIM-ELAB-VHARRAYAGG-007` | error | A VHDL array aggregate would implicitly lose four- or nine-state information in a two-state element. |
| `FSIM-ELAB-VHARRAYAGG-008` | error | A VHDL array aggregate combines `others` with another choice in the same association. |
| `FSIM-ELAB-VHARRAYAGG-009` | error | A VHDL array aggregate element value has an incompatible contextual subtype or state domain. |
| `FSIM-ELAB-VHARRAYSEL-001` | error | A multidimensional or composite VHDL array selection has no concrete executable packed layout or representable shape. |
| `FSIM-ELAB-VHARRAYSEL-002` | error | A runtime multidimensional VHDL array index is not an integer-family signed 32-bit value with representable bounds. |
| `FSIM-ELAB-VHARRAYSEL-003` | error | A multidimensional VHDL array read or target index is outside its selected source dimension. |
| `FSIM-ELAB-VHARRAYSEL-004` | error | A multidimensional VHDL array slice has incompatible direction, bounds, placement, or contextual shape. |
| `FSIM-ELAB-VHARRAYATTR-001` | error | A VHDL array attribute prefix is unknown, nonarray, unconstrained, null, or otherwise lacks a concrete bounded range. |
| `FSIM-ELAB-VHARRAYATTR-002` | error | A VHDL array attribute selects a nonstatic dimension or a dimension outside the concrete array rank. |
| `FSIM-ELAB-VHARRAYATTR-003` | error | VHDL `range` or `reverse_range` is used as a scalar expression rather than a discrete range. |
| `FSIM-ELAB-VHARRAYATTR-004` | error | A scalar VHDL array attribute result is outside the portable signed 32-bit integer representation. |
| `FSIM-ELAB-VHDLMATCH-001` | error | A matching case or selected assignment is outside VHDL-2008 or has a selector outside the bounded bit/std_ulogic scalar or one-dimensional-array domain. |
| `FSIM-ELAB-VHDLMATCH-002` | error | A bounded matching choice is not a locally static bit/std_ulogic literal of the selector width. |
| `FSIM-ELAB-VHDLMATCH-003` | error | Two matching choices overlap after applying `-` wildcard and 0/L or 1/H equivalence. |
| `FSIM-ELAB-VHDLMATCH-004` | error | Matching equality operands are outside the bit/std_ulogic scalar or one-dimensional-array domain. |
| `FSIM-ELAB-VHDLCASE-001` | error | A VHDL case choice or range bound is not a locally static value of the selector's discrete or packed literal type. |
| `FSIM-ELAB-VHDLCASE-002` | error | A VHDL case choice or range lies outside the selector subtype constraint. |
| `FSIM-ELAB-VHDLCASE-003` | error | A VHDL case statement repeats a discrete or packed choice. |
| `FSIM-ELAB-VHDLCASE-004` | error | Two VHDL case choices or nonnull ranges overlap. |
| `FSIM-ELAB-VHDLCASE-005` | error | A VHDL case statement neither covers its complete selector subtype nor supplies `others`. |
| `FSIM-ELAB-VHDLCASE-006` | error | Validated VHDL case-range HIR is malformed or lost its static bounds before lowering. |
| `FSIM-ELAB-VHDLGUARD-001` | error | A guarded concurrent assignment has no enclosing Boolean block guard or no retained assignment leaf. |
| `FSIM-ELAB-VHDLGUARD-002` | error | A guarded driver disconnection targets a local or non-nine-state signal. |
| `FSIM-ELAB-VHENUM-001` | error | A contextual VHDL enumeration type has no matching identifier or character literal. |
| `FSIM-ELAB-VHENUM-002` | error | Assignment or comparison mixes values from different nominal VHDL enumeration types. |
| `FSIM-ELAB-VHENUM-003` | error | An operator that is not defined for VHDL enumeration values was applied to an enumeration object. |
| `FSIM-ELAB-VHENUMATTR-001` | error | A VHDL enumeration scalar attribute has an invalid prefix, arity, argument type, result context, or executable ordinal range. |
| `FSIM-ELAB-VHENUMATTR-002` | error | A locally static or executable VHDL enumeration scalar attribute argument is outside the type's declaration range or has no predecessor/successor. |
| `FSIM-ELAB-VHSCALARATTR-001` | error | A non-enumeration VHDL scalar attribute has an invalid type-mark prefix, arity, argument type, or executable range. |
| `FSIM-ELAB-VHSCALARATTR-002` | error | A locally static or executable VHDL scalar attribute argument is outside its subtype range or has no predecessor/successor. |
| `FSIM-ELAB-VHSCALARATTR-003` | error | A scalar VHDL `range` or `reverse_range` attribute is used as a scalar expression rather than a discrete range. |
| `FSIM-ELAB-VHENUMRANGE-001` | error | A VHDL enumeration subtype constraint bound is not locally static, is unknown, or belongs to a different nominal enumeration type. |
| `FSIM-ELAB-VHENUMRANGE-002` | error | A VHDL enumeration subtype constraint is null or lies outside the base enumeration's literal table. |
| `FSIM-ELAB-VHENUMRANGE-003` | error | A derived VHDL enumeration subtype constraint is not contained by its resolved base subtype. |
| `FSIM-ELAB-VHENUMRANGE-004` | error | A locally static value assigned to a constrained VHDL enumeration object lies outside the subtype range. |
| `FSIM-ELAB-VHPORT-001` | error | A VHDL input-port expression is not a supported locally static value for its contextual formal type. |
| `FSIM-ELAB-VHPORT-002` | error | A VHDL output, buffer, or inout port is associated with an expression that is not a writable signal name. |
| `FSIM-ELAB-SVENUM-001` | error | An enum base width is unsupported or an enumerator value does not fit it. |
| `FSIM-ELAB-SVENUM-002` | error | Two literals in one bounded enum have the same value. |
| `FSIM-ELAB-SVCONST-001` | error | A SystemVerilog parameter value cannot be converted to its declared bounded integral type without losing X/Z state or valid width metadata. |
| `FSIM-ELAB-SVEXPR-001` | error | Streaming-concatenation HIR has the wrong language, arity, or operand shape. |
| `FSIM-ELAB-SVEXPR-002` | error | A streaming-concatenation slice size is not a positive locally constant value within the 64-bit integral contract. |
| `FSIM-ELAB-SVEXPR-003` | error | A streaming concatenation has a container, aggregate, unknown-width, or wider-than-64-bit packed operand/result. |
| `FSIM-ELAB-SVEXPR-004` | error | A runtime-base packed part-select width is not a positive locally constant value from 1 through 64. |
| `FSIM-ELAB-SVEXPR-006` | error | A runtime-base packed part-select target is used with an unsupported assignment kind. |
| `FSIM-ELAB-SVEXPR-007` | error | Update-expression HIR is not a supported SystemVerilog prefix or postfix increment/decrement of one writable operand. |
| `FSIM-ELAB-SVEXPR-008` | error | A runtime-selected packed procedural target has a following selection. |
| `FSIM-ELAB-SVFORCE-001` | error | A procedural force or release target is not a supported signal, static bit-select, static part-select, or packed member. |
| `FSIM-ELAB-SVFORCE-002` | error | A procedural force or release target is not a visible packed signal with an executable layout. |
| `FSIM-ELAB-SVFORCE-003` | error | A four-state value is forced onto a two-state target without explicit conversion. |
| `FSIM-ELAB-SVFILE-001` | error | A bounded SystemVerilog text-file handle is not a 32-bit integer expression. |
| `FSIM-ELAB-SVFILE-002` | error | A file read/error target is not a whole mutable string object or automatic local. |
| `FSIM-ELAB-SVFILE-003` | error | `$fopen` does not have bounded SystemVerilog byte-string filename and mode operands. |
| `FSIM-ELAB-SVFILE-004` | error | `$fgets` does not have a mutable string target and integer handle. |
| `FSIM-ELAB-SVFILE-005` | error | `$feof` does not have one integer handle. |
| `FSIM-ELAB-SVFILE-006` | error | `$ferror` does not have an integer handle and mutable string target. |
| `FSIM-ELAB-SVFILE-007` | error | A bounded formatted file-output value cannot be lowered. |
| `FSIM-ELAB-SVFILE-008` | error | A module integer initializer is not a known 32-bit constant. |
| `FSIM-ELAB-SVFILE-009` | error | `$fgetc` does not have one integer file handle. |
| `FSIM-ELAB-SVFILE-010` | error | `$ungetc` does not have a character expression and integer file handle. |
| `FSIM-ELAB-SVFILE-011` | error | `$fscanf` or `$sscanf` has an invalid source, format, arity, or bounded conversion list. |
| `FSIM-ELAB-SVFILE-012` | error | A formatted scan target is not a direct writable compatible packed or string variable. |
| `FSIM-ELAB-SVFILE-013` | error | `$fread` does not have a compatible integer handle and optional bounded start/count expressions. |
| `FSIM-ELAB-SVFILE-014` | error | A binary-read target is not a direct writable 1..64-bit packed value or one-dimensional fixed integral memory. |
| `FSIM-ELAB-SVFILE-015` | error | `$fseek`, `$ftell`, or `$rewind` has an invalid handle, arity, offset, or origin expression. |
| `FSIM-ELAB-SVFILE-016` | error | `$fflush` has a nonintegral explicit file handle. |
| `FSIM-ELAB-SVCONTAINER-001` | error | A container local is duplicated in one automatic scope. |
| `FSIM-ELAB-SVCONTAINER-002` | error | A container element expression has no resolvable element type. |
| `FSIM-ELAB-SVCONTAINER-003` | error | A container element is a string, unpacked aggregate, unresolved, or outside the executable 1-to-64-bit scalar, enum, or packed-aggregate range. |
| `FSIM-ELAB-SVCONTAINER-004` | error | A bounded queue maximum index is unknown or outside 0 through 4,095. |
| `FSIM-ELAB-SVCONTAINER-005` | error | A container value is not a direct supported object reference. |
| `FSIM-ELAB-SVCONTAINER-006` | error | A container expression references an unknown container object. |
| `FSIM-ELAB-SVCONTAINER-007` | error | A container method receiver is not a direct supported object. |
| `FSIM-ELAB-SVCONTAINER-008` | error | A container uses an unsupported method. |
| `FSIM-ELAB-SVCONTAINER-009` | error | A container assignment is nonblocking or has a timing/event control. |
| `FSIM-ELAB-SVCONTAINER-010` | error | A whole-container assignment is neither `new[size]` nor a compatible container value. |
| `FSIM-ELAB-SVCONTAINER-011` | error | A container target is neither a whole object nor one element index. |
| `FSIM-ELAB-SVCONTAINER-012` | error | A module container uses a declaration initializer instead of an initial block. |
| `FSIM-ELAB-SVCONTAINER-013` | error | An associative-array index is unresolved, aggregate, nonintegral, or outside the executable 1-to-64-bit range. |
| `FSIM-ELAB-SVCONTAINER-014` | error | `new[size]` is used to resize an associative array. |
| `FSIM-ELAB-SVCONTAINER-015` | error | An associative-array query or traversal method is used on another container kind. |
| `FSIM-ELAB-SVCONTAINER-016` | error | An associative-array traversal argument is not a direct mutable integral variable. |
| `FSIM-ELAB-SVCONTAINER-017` | error | An associative-array traversal argument width does not match the index type. |
| `FSIM-ELAB-SVCONTAINER-018` | error | `delete(index)` is used on a container other than a queue or associative array. |
| `FSIM-ELAB-SVCONTAINER-019` | error | A queue-only insert, push, or pop method is used on another container kind. |
| `FSIM-ELAB-SVCONTAINER-020` | error | Static unpacked-array bounds are not locally constant signed 32-bit values spanning 1 through 4,096 elements. |
| `FSIM-ELAB-SVCONTAINER-021` | error | `delete()` is used to clear a fixed static unpacked array. |
| `FSIM-ELAB-SVCONTAINER-022` | error | A mutating container method is applied to a temporary or another non-object receiver. |
| `FSIM-ELAB-SVCONTAINER-023` | error | A dynamic-array `new[size](initializer)` value is not an exactly compatible dynamic array. |
| `FSIM-ELAB-SVCOND-001` | error | A container conditional has invalid arity or is used outside SystemVerilog. |
| `FSIM-ELAB-SVCOND-002` | error | Container conditional alternatives do not have an exactly compatible kind and profile. |
| `FSIM-ELAB-SVCOND-003` | error | An associative-array conditional value is used outside the bounded consumer subset. |
| `FSIM-ELAB-SVCOND-004` | error | A container conditional condition does not produce one bit. |
| `FSIM-ELAB-SVEQUAL-001` | error | A container operand is used with an unsupported comparison operator or outside SystemVerilog. |
| `FSIM-ELAB-SVEQUAL-002` | error | Whole-container equality does not have two typed container operands. |
| `FSIM-ELAB-SVEQUAL-003` | error | Whole-container equality operands do not have an exactly compatible kind and profile. |
| `FSIM-ELAB-SVMEMBER-001` | error | An `inside` membership expression is lowered outside SystemVerilog. |
| `FSIM-ELAB-SVMEMBER-002` | error | An `inside` expression does not contain a left operand and at least one member. |
| `FSIM-ELAB-SVMEMBER-003` | error | The left operand of bounded `inside` membership is not a scalar integral value with statically inferable width. |
| `FSIM-ELAB-SVMEMBER-004` | error | An `inside` member is a nested set, unpacked container, string, aggregate, concatenation, or another unsupported nonintegral value. |
| `FSIM-ELAB-SVMEMBER-005` | error | An `inside` value or range bound does not exactly match the left operand width and signedness. |
| `FSIM-ELAB-SVMEMBER-006` | error | Internal `inside` range metadata does not contain exactly one low and high bound. |
| `FSIM-ELAB-SVCASEINSIDE-001` | error | A `case inside` statement is lowered outside SystemVerilog. |
| `FSIM-ELAB-SVCASEINSIDE-002` | error | A bounded `case inside` selector is not scalar integral or its width is not statically inferable. |
| `FSIM-ELAB-SVCASEINSIDE-003` | error | A `case inside` choice is a nested membership expression, range marker in value position, container, string, aggregate, concatenation, or another unsupported nonintegral value. |
| `FSIM-ELAB-SVCASEINSIDE-004` | error | A `case inside` value or range bound does not exactly match the selector width and signedness. |
| `FSIM-ELAB-SVCASEINSIDE-005` | error | Internal `case inside` HIR contains an alternative without a choice. |
| `FSIM-ELAB-SVCASEINSIDE-006` | error | Internal `case inside` range metadata does not contain exactly one low and high bound. |
| `FSIM-ELAB-SVCASEQUAL-001` | error | Internal case-statement HIR contains an invalid qualifier. |
| `FSIM-ELAB-SVCASEQUAL-002` | error | A qualified case statement is lowered outside SystemVerilog. |
| `FSIM-ELAB-SVMATCH-001` | error | A `case matches` statement is lowered outside SystemVerilog. |
| `FSIM-ELAB-SVMATCH-002` | error | A bounded `case matches` selector is not scalar integral or its width is not statically inferable. |
| `FSIM-ELAB-SVMATCH-003` | error | A bounded `case matches` item is neither a scalar integral constant pattern nor the `.*` wildcard. |
| `FSIM-ELAB-SVMATCH-004` | error | A bounded constant pattern does not exactly match the selector width and signedness. |
| `FSIM-ELAB-SVMATCH-005` | error | Internal `case matches` HIR contains an item without exactly one pattern. |
| `FSIM-ELAB-SVQUERY-001` | error | A bounded unpacked-container query has the wrong language or arity, an indirect expression, or no direct typed container object. |
| `FSIM-ELAB-SVQUERY-002` | error | A bounded unpacked-container query dimension is not the locally constant dimension `1`. |
| `FSIM-ELAB-SVQUERY-003` | error | A finite-bound query is applied to an associative array. |
| `FSIM-ELAB-SVQUERY-004` | error | A type-only SystemVerilog query is outside the bounded container-query subset. |
| `FSIM-ELAB-SVPATTERN-001` | error | A container assignment pattern has inconsistent metadata or uses keyed versus positional members with the wrong container kind. |
| `FSIM-ELAB-SVPATTERN-002` | error | A static pattern has the wrong element count or a dynamic pattern exceeds its container capacity. |
| `FSIM-ELAB-SVPATTERN-003` | error | An associative pattern key is not locally constant and known or duplicates another converted key. |
| `FSIM-ELAB-SVPATTERN-004` | error | A container assignment pattern mixes positional members with keyed/default members or uses `default` outside the bounded static-array subset. |
| `FSIM-ELAB-SVPATTERN-005` | error | A keyed/default static-array assignment pattern is missing its one required default member or contains duplicate defaults. |
| `FSIM-ELAB-SVPATTERN-006` | error | A static-array assignment-pattern index key is not a locally constant known integral value. |
| `FSIM-ELAB-SVPATTERN-007` | error | A static-array assignment-pattern index key is duplicate after signed conversion or outside the declared range. |
| `FSIM-ELAB-SVSLICE-001` | error | An unpacked slice is not a direct colon selection of a one-dimensional static array. |
| `FSIM-ELAB-SVSLICE-002` | error | A static-array slice bound is not a locally constant known integral value. |
| `FSIM-ELAB-SVSLICE-003` | error | A static-array slice reverses its declared direction or selects an out-of-range index. |
| `FSIM-ELAB-SVSLICE-004` | error | A static-array slice assignment source or destination is not a supported whole fixed array or direct fixed-array slice. |
| `FSIM-ELAB-SVSLICE-005` | error | Static-array slice assignment source and destination element counts differ. |
| `FSIM-ELAB-SVSLICE-006` | error | Static-array slice assignment source and destination element width, signedness, or state domains differ. |
| `FSIM-ELAB-SVREDUCE-001` | error | A container reduction is outside SystemVerilog-2017 or lacks a direct supported unpacked-container receiver. |
| `FSIM-ELAB-SVREDUCE-002` | error | A container reduction method has an argument or more than one retained `with` transformation. |
| `FSIM-ELAB-SVREDUCE-003` | error | A result-producing container reduction is used as a standalone statement. |
| `FSIM-ELAB-SVREDUCE-004` | error | A reduction `with` transformation uses unsupported arithmetic, calls, side effects, nonconstant operands, more than one conditional, or more than 64 graph nodes. |
| `FSIM-ELAB-SVREDUCE-005` | error | A reduction `with` transformation contains an invalid iterator/index reference or mixes element and index comparison profiles. |
| `FSIM-ELAB-SVREDUCE-006` | error | A reduction `with` transformation has a non-element root or uses an associative or otherwise excluded receiver profile. |
| `FSIM-ELAB-SVREDUCE-007` | error | A named reduction transformation iterator is malformed or collides with a visible object. |
| `FSIM-ELAB-SVORDER-001` | error | Container ordering lacks a direct writable supported SystemVerilog unpacked-container receiver. |
| `FSIM-ELAB-SVORDER-002` | error | A container ordering method has an invalid retained receiver, optional iterator, or key-expression shape. |
| `FSIM-ELAB-SVORDER-003` | error | A container ordering method is applied to an associative array. |
| `FSIM-ELAB-SVORDER-004` | error | A void container ordering method is used as an expression result. |
| `FSIM-ELAB-SVORDER-005` | error | Nondeterministic `shuffle()` is outside the bounded container-ordering subset. |
| `FSIM-ELAB-SVORDER-006` | error | A `sort` or `rsort` key uses unsupported arithmetic, calls, side effects, nonconstant operands, more than one conditional, or more than 64 graph nodes. |
| `FSIM-ELAB-SVORDER-007` | error | A `sort` or `rsort` key contains an invalid iterator/index reference or mixes element and index comparison profiles. |
| `FSIM-ELAB-SVORDER-008` | error | A `sort` or `rsort` key has a malformed or colliding named iterator, a non-element root, or an otherwise excluded receiver profile. |
| `FSIM-ELAB-SVLOCATOR-001` | error | A container locator lacks a direct supported nonassociative SystemVerilog unpacked-container receiver. |
| `FSIM-ELAB-SVLOCATOR-002` | error | A bounded container locator method has one or more arguments. |
| `FSIM-ELAB-SVLOCATOR-003` | error | A container locator result target is not a compatible queue. |
| `FSIM-ELAB-SVLOCATOR-004` | error | A container locator result is used outside a whole-queue assignment. |
| `FSIM-ELAB-SVLOCATOR-005` | error | A result-producing container locator is used as a standalone statement. |
| `FSIM-ELAB-SVLOCATOR-006` | error | An extrema/uniqueness locator transformation uses unsupported arithmetic, calls, side effects, nonconstant operands, more than one conditional, or more than 64 graph nodes. |
| `FSIM-ELAB-SVLOCATOR-007` | error | An extrema/uniqueness locator transformation contains an invalid iterator/index reference or mixes element and index comparison profiles. |
| `FSIM-ELAB-SVLOCATOR-008` | error | An extrema/uniqueness locator transformation has a malformed or colliding named iterator, a non-element root, or an associative or otherwise excluded receiver profile. |
| `FSIM-ELAB-SVFIND-001` | error | A predicate container locator lacks a direct supported nonassociative SystemVerilog unpacked-container receiver. |
| `FSIM-ELAB-SVFIND-002` | error | A predicate container locator does not retain exactly one `with`-clause predicate. |
| `FSIM-ELAB-SVFIND-003` | error | A predicate container locator result target is not a compatible value or signed 32-bit index queue. |
| `FSIM-ELAB-SVFIND-004` | error | A predicate container locator uses an unsupported, nonconstant, mistyped, or overlarge predicate expression. |
| `FSIM-ELAB-SVFIND-005` | error | A result-producing predicate container locator is used as a standalone statement. |
| `FSIM-ELAB-SVFIND-006` | error | A predicate container locator result is used outside a whole-queue assignment. |
| `FSIM-ELAB-SVFIND-007` | error | A named predicate-container iterator is malformed or collides with a visible object. |
| `FSIM-ELAB-SVFIND-008` | error | A predicate-container iterator has an unknown or unsupported index reference, selection, call, or mixed element/index comparison profile. |
| `FSIM-ELAB-SVMEMORY-001` | error | A `$readmem*` or `$writemem*` task is lowered outside SystemVerilog-2017. |
| `FSIM-ELAB-SVMEMORY-002` | error | A memory-file task file name is not a bounded string expression. |
| `FSIM-ELAB-SVMEMORY-003` | error | A memory-file task object is not a direct bounded static unpacked-array object. |
| `FSIM-ELAB-SVMEMORY-004` | error | A memory-file task start or finish argument is not a 32-bit integral expression. |
| `FSIM-ELAB-SVPORT-001` | error | A SystemVerilog container port lacks a supported one-dimensional integral element or associative-index type. |
| `FSIM-ELAB-SVPORT-002` | error | A bounded-queue maximum or static-array range does not specialize within the 4,096-element limit. |
| `FSIM-ELAB-SVPORT-003` | error | A container input port attempts to use an unsupported default connection value. |
| `FSIM-ELAB-SVPORT-004` | error | A SystemVerilog container port attempts to cross a language boundary. |
| `FSIM-ELAB-SVPORT-005` | error | A container-port actual is an expression, selection, or slice rather than a direct whole-container object. |
| `FSIM-ELAB-SVPORT-006` | error | A container-port actual is unknown, unresolved, or omitted from a required input connection. |
| `FSIM-ELAB-SVPORT-007` | error | A container formal and actual differ in kind, element/index profile, queue bound, or exact static range. |
| `FSIM-ELAB-SVPORT-008` | error | Independent output or inout module-port paths drive the same container object. |
| `FSIM-ELAB-SVPORT-009` | error | An input container port would be modified locally or through a descendant output/inout connection. |
| `FSIM-ELAB-SVPORT-010` | error | A mutable string module port is unconnected, crosses a language boundary, or does not use a direct same-language string object actual. |
| `FSIM-ELAB-SVPORT-011` | error | A mutable string input port is written directly or through a descendant output/inout port. |
| `FSIM-ELAB-SVPORT-012` | error | Sibling output/inout mutable string ports drive the same object. |
| `FSIM-ELAB-SVSTRING-001` | error | A SystemVerilog string parameter/localparam default is not a supported immutable constant-string expression. |
| `FSIM-ELAB-SVSTRING-002` | error | A SystemVerilog parameter actual crosses the bounded integral/string type boundary or is not a supported constant string. |
| `FSIM-ELAB-SVSTRING-003` | error | A bounded output/report message position contains a string expression that is not constant after specialization. |
| `FSIM-ELAB-SVSTRING-005` | error | A runtime string variable is duplicated in one automatic scope. |
| `FSIM-ELAB-SVSTRING-006` | error | A runtime string literal has no retained decoded byte value. |
| `FSIM-ELAB-SVSTRING-007` | error | A runtime string literal, initializer, object, or concatenation exceeds the 4,096-byte v1 limit. |
| `FSIM-ELAB-SVSTRING-008` | error | A runtime string expression references an unknown string object. |
| `FSIM-ELAB-SVSTRING-009` | error | A runtime string concatenation has no operands. |
| `FSIM-ELAB-SVSTRING-010` | error | A runtime string concatenation or value context contains a non-string operand. |
| `FSIM-ELAB-SVSTRING-011` | error | A runtime string function call has an unknown or incompatible callable profile. |
| `FSIM-ELAB-SVSTRING-012` | error | Runtime string equality or inequality mixes string and non-string operands. |
| `FSIM-ELAB-SVSTRING-013` | error | A runtime string assignment is nonblocking or has a timing/event control. |
| `FSIM-ELAB-SVSTRING-014` | error | A whole-string assignment has a non-string right-hand side. |
| `FSIM-ELAB-SVSTRING-015` | error | A runtime string assignment target is neither a whole object nor one byte index. |
| `FSIM-ELAB-SVSTRING-016` | error | A retained module variable is not in the supported runtime string domain. |
| `FSIM-ELAB-SVSTRING-017` | error | A module string initializer is not a supported bounded static string expression. |
| `FSIM-ELAB-SVSTRING-018` | error | A runtime string method has an unsupported receiver, argument, or result context. |
| `FSIM-ELAB-SVSTRING-019` | error | `$swrite`, `$sformat`, or `$sformatf` has a nonliteral, malformed, oversized, under-supplied, or unsupported bounded format/value list. |
| `FSIM-ELAB-SVSTRING-020` | error | `$swrite` or `$sformat` does not name a direct writable string target with positional arguments. |
| `FSIM-ELAB-SVTYPEPARAM-001` | error | A required SystemVerilog type parameter or local type parameter has no data-type actual/default. |
| `FSIM-ELAB-SVTYPEPARAM-002` | error | A SystemVerilog value parameter received a data-type actual. |
| `FSIM-ELAB-SVTYPEPARAM-003` | error | A SystemVerilog type-parameter actual/default is invisible or outside the bounded packed integral subset. |
| `FSIM-ELAB-SVTYPEPARAM-004` | error | A SystemVerilog type parameter was associated across a mixed-language boundary instead of through a same-language wrapper. |
| `FSIM-ELAB-SVSTRUCT-001` | error | A packed-struct member range or total layout cannot be specialized into a supported width. |
| `FSIM-ELAB-SVSTRUCT-002` | error | A packed-aggregate member read/write has no executable normalized layout. |
| `FSIM-ELAB-SVUNION-001` | error | Packed-union members do not specialize to one common nonzero supported width. |
| `FSIM-ELAB-SVAGG-001` | error | An aggregate assignment pattern has inconsistent contextual layout or association metadata. |
| `FSIM-ELAB-SVAGG-002` | error | An aggregate assignment-pattern key is not a direct member name or names an unknown member. |
| `FSIM-ELAB-SVAGG-003` | error | An aggregate assignment pattern duplicates, omits, or supplies too many members or defaults. |
| `FSIM-ELAB-SVAGG-004` | error | An aggregate assignment-pattern member value has the wrong width. |
| `FSIM-ELAB-SVAGG-005` | error | A two-state aggregate assignment-pattern member receives a four-state value without conversion. |
| `FSIM-ELAB-SVAGG-006` | error | A packed-union assignment pattern does not select exactly one member or uses `default`. |
| `FSIM-ELAB-SVCAST-001` | error | Malformed cast HIR does not contain exactly one operand. |
| `FSIM-ELAB-SVCAST-002` | error | A SystemVerilog cast names a type that is not visible. |
| `FSIM-ELAB-SVCAST-003` | error | A SystemVerilog cast type has no executable width in 1..64. |
| `FSIM-ELAB-SVCAST-004` | error | A four-state-to-two-state cast is outside the bounded aggregate cast slice. |
| `FSIM-ELAB-SVMDARRAY-001` | error | A multidimensional static-array access does not supply exactly one index per declared unpacked dimension. |
| `FSIM-ELAB-SVMDARRAY-002` | error | A runtime multidimensional static-array index cannot lower to a signed 32-bit integral value. |
| `FSIM-ELAB-SVMDARRAY-003` | error | A multidimensional static-array index is outside its declared range or cannot be flattened within the bounded capacity. |
| `FSIM-ELAB-SVREPL-001` | error | A replication concatenation has a nonconstant/nonpositive count, no statically sized operands, or an overflowing expanded width. |
| `FSIM-ELAB-GENERIC-001` | error | A VHDL generic actual is unknown, missing, excessive, or cannot target the selected SystemC factory. |
| `FSIM-ELAB-GENERIC-002` | error | A VHDL generic receives more than one actual. |
| `FSIM-ELAB-GENERIC-003` | error | A positional VHDL generic actual follows a named actual. |
| `FSIM-ELAB-GENERIC-004` | error | A VHDL generic actual constant expression cannot be evaluated. |
| `FSIM-ELAB-GENERIC-005` | error | A VHDL generic default constant expression cannot be evaluated. |
| `FSIM-ELAB-GENERIC-006` | error | A generic-dependent packed range cannot be evaluated. |
| `FSIM-ELAB-GENERIC-007` | error | A generic-dependent packed range width overflows the supported range. |
| `FSIM-ELAB-GENERIC-008` | error | A generic value violates its bounded scalar subtype constraint. |
| `FSIM-ELAB-GENERIC-009` | error | An architecture signal conflicts with an entity generic. |
| `FSIM-ELAB-GENERIC-010` | error | A bounded subtype-typed VHDL generic resolves outside the supported scalar, physical-time, or up-to-64-bit packed value set. |
| `FSIM-ELAB-GENTYPE-001` | error | A required VHDL interface type generic has no associated subtype indication. |
| `FSIM-ELAB-GENTYPE-002` | error | A VHDL interface type generic actual is not syntactically a subtype indication. |
| `FSIM-ELAB-GENTYPE-003` | error | A VHDL interface type generic actual names a type that is not visible at the association. |
| `FSIM-ELAB-GENTYPE-004` | error | A VHDL interface type generic is given an actual through a non-VHDL association boundary. |
| `FSIM-ELAB-GENTYPE-005` | error | A VHDL value generic is given an unambiguous subtype-indication actual. |
| `FSIM-ELAB-PARAM-001` | error | A parameter override has an unknown/local target, is missing or excessive, or is applied to a SystemC factory. |
| `FSIM-ELAB-PARAM-002` | error | A parameter override is duplicated during elaboration. |
| `FSIM-ELAB-PARAM-003` | error | Named and positional parameter overrides are mixed during elaboration. |
| `FSIM-ELAB-PARAM-004` | error | A parameter override constant expression cannot be evaluated. |
| `FSIM-ELAB-PARAM-005` | error | A parameter default constant expression cannot be evaluated. |
| `FSIM-ELAB-PARAM-006` | error | A parameterized packed range cannot be evaluated. |
| `FSIM-ELAB-PARAM-007` | error | A parameterized packed range width overflows the supported range. |
| `FSIM-ELAB-PARAM-009` | error | A case-insensitive VHDL named generic actual ambiguously matches multiple case-sensitive parameters on a foreign Verilog/SystemVerilog target. |
| `FSIM-ELAB-SC-PARAM-001` | error | A SystemC construction actual is unknown, missing, or excessive. |
| `FSIM-ELAB-SC-PARAM-002` | error | A SystemC construction parameter receives more than one actual. |
| `FSIM-ELAB-SC-PARAM-003` | error | Construction actual association ordering is illegal for the HDL parent language. |
| `FSIM-ELAB-SC-PARAM-004` | error | A SystemC construction actual constant expression cannot be evaluated in the parent specialization. |
| `FSIM-ELAB-SC-PARAM-005` | error | A construction value violates the factory parameter's integer/natural/positive/Boolean/bit subtype. |
| `FSIM-ELAB-SC-PARAM-006` | error | A VHDL named actual ambiguously matches case-distinct SystemC factory parameters. |
| `FSIM-ELAB-SC-PARAM-007` | error | The selected SystemC factory schema cannot be inspected. |
| `FSIM-ELAB-SC-PARAM-008` | error | The schema-validated SystemC instance cannot be constructed or has inconsistent provider identity. |
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
| `FSIM-ELAB-BIND-027` | error | A boundary actual is not a whole signal, an input is illegally open, or a required VHDL direct-entity input is unassociated. |
| `FSIM-ELAB-BIND-028` | error | A boundary actual names an unknown parent signal. |
| `FSIM-ELAB-BIND-030` | error | A cross-language `inout` lacks an explicit resolver. |
| `FSIM-ELAB-BIND-031` | error | Same-language VHDL packed-array boundary bounds or direction differ. |
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
| `FSIM-ELAB-BIND-042` | error | A registered SystemC thread process requires fiber support, but this build configured `FSIM_SYSTEMC_FIBER_MODE=OFF`. |
| `FSIM-ELAB-BIND-043` | error | A SystemC process sensitivity references an unknown registered object. |
| `FSIM-ELAB-BIND-044` | error | A SystemC process registered an invalid sensitivity edge. |
| `FSIM-ELAB-BIND-045` | error | A SystemC edge sensitivity references a non-scalar object. |
| `FSIM-ELAB-BIND-046` | error | A SystemC port/channel binding references an unknown internal signal or aliases one internal signal to conflicting parent signals. |
| `FSIM-ELAB-BIND-047` | error | A constructed native SystemC child has an inconsistent parent handle or direct-child path. |
| `FSIM-ELAB-BIND-048` | error | A typed SystemC export is unbound, cyclic, references an unknown object, or conflicts with another hierarchy alias. |
| `FSIM-ELAB-BIND-049` | error | A packed aggregate crosses a language boundary without a same-language scalar/vector wrapper. |
| `FSIM-ELAB-BIND-050` | error | An elaboration binding names a resolver other than `std_logic` or `sv_wire`. |
| `FSIM-ELAB-BIND-051` | error | An integer hierarchy boundary cannot guarantee a range-safe same-language alias or cross-language 32-bit signed conversion in the port's data-flow direction. |
| `FSIM-ELAB-BIND-052` | error | A VHDL enumeration crosses a language boundary without a same-language scalar/vector wrapper. |
| `FSIM-ELAB-BIND-053` | error | A same-language VHDL hierarchy boundary connects different nominal enumeration types. |
| `FSIM-ELAB-BIND-054` | error | Same-language VHDL enumeration subtype ranges cannot guarantee a range-safe alias in the port's data-flow direction. |
| `FSIM-ELAB-BIND-055` | error | A VHDL array crosses a language boundary without a same-language scalar/vector wrapper. |
| `FSIM-ELAB-BIND-056` | error | A same-language VHDL hierarchy boundary connects different nominal array types. |
| `FSIM-ELAB-BIND-057` | error | A same-language hierarchy boundary connects different nominal aggregate or record types. |
| `FSIM-ELAB-BIND-058` | error | A native SystemC child port is unbound after factory construction. |

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
| `FSIM-HDL-REPORT` | report severity | A VHDL report or SystemVerilog severity task was emitted through a command or Tcl output stream. |
| `FSIM-RUN-DELTA-0001` | error | Simulation exceeded `max_deltas`; the message includes pending processes and recent signals. |
| `FSIM-VCD-0001` | error | The trace output directory could not be created. |
| `FSIM-VCD-0002` | error | The VCD trace file could not be opened. |
| `FSIM-VCD-0003` | error | VCD declaration, value emission, timestamp scaling, or flushing failed. |
| `FSIM-CACHE-0001` | error | A parsed source could not be hashed or associated with its cache provenance. |
| `FSIM-CACHE-0002` | warning | An unreadable or incompatible design-cache entry was discarded. |
| `FSIM-CACHE-0003` | error | A design-cache entry could not be populated. |
| `FSIM-CACHE-0004` | warning | Native-object cache load, store, or prune failures prevented complete cache reuse or eviction. |

## SystemC source compiler and plug-in validation

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-SC-A001` | error | A validated SystemC plug-in registered no module factory. |
| `FSIM-SC-A002` | error | A requested SystemC hierarchy has no compiled plug-in or registered factory. |
| `FSIM-SC-A003` | error | A requested SystemC factory uses the legacy untyped construction ABI. |
| `FSIM-SC-A004` | error | A typed SystemC factory failed during module construction. |
| `FSIM-SC-A005` | error | One SystemC instance path has conflicting factory targets. |
| `FSIM-SC-A006` | error | A registered SystemC object could not be bound to its common-runtime signal. |
| `FSIM-SC-A007` | error | The selected project time resolution cannot configure the SystemC runtime. |
| `FSIM-SC-A008` | error | A SystemC elaboration lifecycle callback failed or the selected lifecycle roots were inconsistent. |
| `FSIM-SC-C001` | error | The SystemC compiler working directory cannot be resolved. |
| `FSIM-SC-C002` | error | A SystemC source set contains no C++ sources. |
| `FSIM-SC-C003` | error | A SystemC source or dependency is missing, unreadable, invalid, or cannot be hashed/scanned. |
| `FSIM-SC-C004` | error | A compiler option conflicts with fsim's shared-library build contract. |
| `FSIM-SC-C005` | error | A SystemC plug-in cache/lock/staging directory cannot be created, inspected, or cleaned safely. |
| `FSIM-SC-C006` | error | The per-key SystemC plug-in cache lock cannot be acquired. |
| `FSIM-SC-C007` | error | The host compiler is unavailable, failed to start or complete, exited unsuccessfully, or produced no shared library. |
| `FSIM-SC-C008` | error | A compiled plug-in or its versioned key/size/checksum commit record cannot be hashed, written, or atomically published. |
| `FSIM-SC-C009` | error | A compiled SystemC plug-in failed image load, entry-point, ABI, initialization, or transactional registration validation. |
| `FSIM-SC-C010` | error | An explicit linked library is missing or cannot be content-hashed. |
| `FSIM-SC-C011` | error | A raw compiler option hides inputs from the persistent cache dependency model. |
| `FSIM-SC-C012` | warning | Compiler identity, dependency closure, a raw external input, or a volatile predefined macro prevents safe persistent plug-in cache reuse. |
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
