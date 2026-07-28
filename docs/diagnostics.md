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
| `FSIM-TCL-0001` | error | The selected build has no embedded Tcl interface. |
| `FSIM-TCL-0002` | error | The Tcl interpreter, standard library, fsim namespace, or standard channels could not be initialized. |
| `FSIM-TCL-0003` | error | A batch Tcl command or script failed during evaluation. |
| `FSIM-TCL-0004` | error | Interactive input ended with an incomplete Tcl command. |
| `FSIM-TCL-ASSERT-0001` | note/warning/error/fatal | An HDL assertion failed while a Tcl-controlled simulation was running. |
| `FSIM-API-0002` | error | A C API check or build was requested before loading a project. |
| `FSIM-API-0003` | error | A C API operation requires a successfully built design. |
| `FSIM-API-0004` | error | A built design contains more debug-visible local variables than the version-1 object-handle encoding can represent. |
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
| `FSIM-FE-CU-0001` | warning | A VHDL source set requested Verilog-style compilation-unit grouping; VHDL files remain independent analysis units. |
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

### VHDL semantics and bounded-subset rejections

| Code | Severity | Meaning |
|---|---|---|
| `FSIM-VHDL-SEM-002` | error | Duplicate port declaration. |
| `FSIM-VHDL-SEM-003` | error | Duplicate signal declaration. |
| `FSIM-VHDL-SEM-004` | error | A delay magnitude is not an integer literal. |
| `FSIM-VHDL-SEM-011` | error | An assertion severity is not `note`, `warning`, `error`, or `failure`. |
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
| `FSIM-VHDL-UNSUPPORTED-001` | error | Unsupported design unit or context item. |
| `FSIM-VHDL-UNSUPPORTED-003` | error | Unsupported entity declaration. |
| `FSIM-VHDL-UNSUPPORTED-004` | error | Unsupported architecture declaration. |
| `FSIM-VHDL-UNSUPPORTED-005` | error | Unsupported labeled concurrent statement. |
| `FSIM-VHDL-UNSUPPORTED-006` | error | Unsupported concurrent statement. |
| `FSIM-VHDL-UNSUPPORTED-007` | error | A process declarative item is not a bounded variable declaration. |
| `FSIM-VHDL-UNSUPPORTED-008` | error | Unsupported sequential statement. |
| `FSIM-VHDL-UNSUPPORTED-010` | error | A port-map actual is not a simple identifier. |
| `FSIM-VHDL-UNSUPPORTED-011` | error | Port default expressions are parsed but not executable. |
| `FSIM-VHDL-UNSUPPORTED-012` | error | Signal initializers are parsed but not executable. |
| `FSIM-VHDL-UNSUPPORTED-013` | error | A subtype requires semantic type resolution not implemented in this slice. |
| `FSIM-VHDL-UNSUPPORTED-014` | error | A non-generic integer-family object is parsed but not executable. |
| `FSIM-VHDL-UNSUPPORTED-015` | error | A nested context declaration appears where only a context reference is permitted. |
| `FSIM-VHDL-UNSUPPORTED-017` | error | A wait is nested in conditional control flow requiring suspension-path analysis. |
| `FSIM-VHDL-UNSUPPORTED-018` | error | A generic type is outside the bounded scalar integer, Boolean, and bit subset. |
| `FSIM-VHDL-UNSUPPORTED-019` | error | An `open` generic actual is not implemented. |
| `FSIM-VHDL-UNSUPPORTED-020` | error | A generate branch contains an item outside the bounded constant, local-signal, assignment, process, instance, and nested-generate subset. |
| `FSIM-VHDL-UNSUPPORTED-021` | error | Guarded VHDL block statements are not executable yet. |
| `FSIM-VHDL-UNSUPPORTED-022` | error | A package body or package declaration outside the bounded constant-only subset is not implemented. |
| `FSIM-VHDL-UNSUPPORTED-023` | error | A package constant is outside the scalar integer, Boolean, or bit subset. |
| `FSIM-VHDL-UNSUPPORTED-024` | error | A context declaration contains an item other than a library clause, use clause, or context reference. |

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
| `FSIM-SV-PP-020` | error | A macro-expanded include name does not produce exactly one token. |
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
| `FSIM-SV-PARSE-062` | error | Expected `:` before a generate-block label. |
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
| `FSIM-SV-PARSE-114` | error | Expected a literal message after a SystemVerilog `$fatal` finish argument. |
| `FSIM-SV-PARSE-115` | error | Expected `)` after SystemVerilog `$fatal` arguments. |
| `FSIM-SV-PARSE-116` | error | Expected `;` after a SystemVerilog `$fatal` task. |
| `FSIM-SV-PARSE-117` | error | Expected `;` after a named-event declaration. |
| `FSIM-SV-PARSE-118` | error | Expected `;` after an immediate named-event trigger. |
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
| `FSIM-SV-SEM-027` | error | A bounded procedural loop condition is not a canonical comparison of its loop variable and a locally static bound. |
| `FSIM-SV-SEM-028` | error | A bounded procedural loop iteration updates a name other than its loop variable. |
| `FSIM-SV-SEM-029` | error | A bounded procedural loop update is not a unit step toward its comparison bound. |
| `FSIM-SV-SEM-030` | error | A bounded `forever` body has no timing control and therefore cannot suspend its process. |
| `FSIM-SV-SEM-031` | error | A SystemVerilog `break` or `continue` statement appears outside a procedural loop. |
| `FSIM-SV-SEM-032` | error | A SystemVerilog `final` procedure contains a timing control, wait, or `$finish`. |
| `FSIM-SV-SEM-033` | error | A SystemVerilog `final` procedure contains a nonblocking assignment. |
| `FSIM-SV-SEM-034` | error | A procedural block closing label has no opening label or does not match it. |
| `FSIM-SV-SEM-035` | error | A named event conflicts with another event, signal, or port declaration. |
| `FSIM-SV-UNSUPPORTED-001` | error | Unsupported compilation-unit item. |
| `FSIM-SV-UNSUPPORTED-002` | error | A raw parser input contains a directive that was not consumed by preprocessing. |
| `FSIM-SV-UNSUPPORTED-004` | error | Unsupported module item. |
| `FSIM-SV-UNSUPPORTED-005` | error | A named port connection was used where a module-header declaration is required. |
| `FSIM-SV-UNSUPPORTED-006` | error | Unpacked port dimensions are not implemented. |
| `FSIM-SV-UNSUPPORTED-007` | error | Unpacked arrays are not implemented. |
| `FSIM-SV-UNSUPPORTED-008` | error | Unsupported identifier-starting procedural statement. |
| `FSIM-SV-UNSUPPORTED-009` | error | Unsupported procedural statement. |
| `FSIM-SV-UNSUPPORTED-010` | error | ANSI port default expressions are parsed but not executable. |
| `FSIM-SV-UNSUPPORTED-011` | error | Declaration initializers are parsed but not executable. |
| `FSIM-SV-UNSUPPORTED-012` | error | Integer objects are parsed but not executable. |
| `FSIM-SV-UNSUPPORTED-014` | error | A procedural declaration uses the net-only `wire` type. |
| `FSIM-SV-UNSUPPORTED-017` | error | A `case` statement uses an unsupported `unique`, `unique0`, or `priority` qualifier. |
| `FSIM-SV-UNSUPPORTED-018` | error | A `case inside` statement requires unsupported set-membership matching. |
| `FSIM-SV-UNSUPPORTED-019` | error | Type parameters are not implemented. |
| `FSIM-SV-UNSUPPORTED-020` | error | A parameter data type is outside the supported integral subset. |
| `FSIM-SV-UNSUPPORTED-021` | error | A generate region or branch contains an item outside the bounded integral-parameter, local-signal, continuous assignment, process, instance, and nested-generate subset. |
| `FSIM-SV-UNSUPPORTED-022` | error | A generated local declaration incorrectly uses a module-port direction. |
| `FSIM-SV-UNSUPPORTED-023` | error | A package item is outside the bounded integral parameter/localparam and import subset. |
| `FSIM-SV-UNSUPPORTED-024` | error | A bounded typedef target is not an integral built-in or user-defined type. |
| `FSIM-SV-UNSUPPORTED-025` | error | An unpacked typedef dimension is outside the current packed alias subset. |
| `FSIM-SV-UNSUPPORTED-026` | error | A bounded enum typedef lacks an explicit packed `bit`, `logic`, or `reg` base type. |
| `FSIM-SV-UNSUPPORTED-027` | error | A bounded struct typedef omits the `packed` qualifier. |
| `FSIM-SV-UNSUPPORTED-028` | error | A packed-struct member uses a nested aggregate or unsupported data type. |
| `FSIM-SV-UNSUPPORTED-029` | error | A packed-struct member has an unpacked dimension or initializer. |
| `FSIM-SV-UNSUPPORTED-030` | error | A built-in gate declaration uses unsupported drive strengths. |
| `FSIM-SV-UNSUPPORTED-031` | error | A procedural `for` loop does not declare an inline `int` or `integer` index. |

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
| `FSIM-ELAB-046` | error | A procedural blocking intra-assignment delay cannot yet suspend after RHS evaluation. |
| `FSIM-ELAB-047` | error | Assignment target and expression widths differ. |
| `FSIM-ELAB-048` | error | A VHDL `if` condition does not have scalar Boolean type. |
| `FSIM-ELAB-049` | error | Binary operands have different widths and would require implicit sizing. |
| `FSIM-ELAB-050` | error | Assignment into a two-state target would implicitly lose four- or nine-state values. |
| `FSIM-ELAB-051` | error | A VHDL assertion condition does not have scalar Boolean type. |
| `FSIM-ELAB-052` | error | A local variable has no executable packed width. |
| `FSIM-ELAB-053` | error | A local-variable declaration duplicates another declaration in the same lexical scope. |
| `FSIM-ELAB-054` | error | A local variable initializer has the wrong packed width. |
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
| `FSIM-ELAB-068` | error | A bit/part select does not have constant in-range bounds, a positive indexed width, or a direction compatible with its declared packed range. |
| `FSIM-ELAB-069` | error | A concatenation is empty or has an operand/result width that cannot be inferred or represented. |
| `FSIM-ELAB-070` | error | A VHDL packed shift or rotate count is not locally static in the current executable slice. |
| `FSIM-ELAB-071` | error | A sequential VHDL for-loop initial bound is not locally static. |
| `FSIM-ELAB-072` | error | A sequential VHDL for-loop final bound is not locally static. |
| `FSIM-ELAB-073` | error | A sequential VHDL for loop exceeds the bounded one-million-iteration elaboration limit. |
| `FSIM-ELAB-074` | error | A sequential for-loop body assigns its VHDL implicit constant or statically substituted SystemVerilog index. |
| `FSIM-ELAB-075` | error | A Verilog/SystemVerilog repeat count is not locally static. |
| `FSIM-ELAB-076` | error | A Verilog/SystemVerilog repeat count is negative. |
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
| `FSIM-ELAB-093` | error | A VHDL array attribute has no representable static packed range or selects an unsupported dimension. |
| `FSIM-ELAB-094` | error | The VHDL `'event` attribute does not name one visible signal. |
| `FSIM-ELAB-095` | error | The VHDL `'last_value` attribute does not name one visible signal. |
| `FSIM-ELAB-096` | error | The VHDL `'last_event` attribute does not name one visible signal. |
| `FSIM-ELAB-097` | error | The bounded zero-duration VHDL `'stable` attribute does not name one visible signal or supplies a duration. |
| `FSIM-ELAB-098` | error | The VHDL `'active` attribute does not name one visible signal. |
| `FSIM-ELAB-099` | error | A named-event trigger does not contain a simple event name. |
| `FSIM-ELAB-100` | error | A named-event trigger references an unknown event. |
| `FSIM-ELAB-101` | error | A named-event trigger targets an object not declared as an event. |
| `FSIM-ELAB-DRV-001` | error | A signal has multiple process drivers, but driver-slot resolution is not executable. |
| `FSIM-ELAB-HIER-001` | error | Duplicate elaborated instance path. |
| `FSIM-ELAB-HIER-002` | error | Recursive instantiation was detected. |
| `FSIM-ELAB-GEN-001` | error | A conditional-generate expression cannot be evaluated for its specialization. |
| `FSIM-ELAB-GEN-002` | error | A loop-generate initial value cannot be evaluated for its specialization. |
| `FSIM-ELAB-GEN-003` | error | A loop-generate continuation condition cannot be evaluated. |
| `FSIM-ELAB-GEN-004` | error | A loop generate exceeds the bounded one-million-iteration elaboration limit. |
| `FSIM-ELAB-GEN-005` | error | A loop-generate iteration expression cannot be evaluated. |
| `FSIM-ELAB-GEN-006` | error | A loop-generate iteration does not advance its variable. |
| `FSIM-ELAB-GEN-007` | error | A nested loop-generate variable shadows an enclosing constant in the bounded executable slice. |
| `FSIM-ELAB-GEN-008` | error | A selection-generate selector cannot be evaluated for its specialization. |
| `FSIM-ELAB-GEN-009` | error | A selection-generate scalar choice or range bound cannot be evaluated. |
| `FSIM-ELAB-GEN-010` | error | Selection-generate scalar/range choices overlap or contain duplicate defaults. |
| `FSIM-ELAB-GEN-011` | error | A generated constant or parameter cannot be evaluated in its declaration-order environment. |
| `FSIM-ELAB-GEN-012` | error | A generated constant/parameter violates a bounded scalar subtype or exceeds the 64-bit integral width. |
| `FSIM-ELAB-PKG-001` | error | A bounded VHDL package import is not `library.package.all` or `library.package.constant`. |
| `FSIM-ELAB-PKG-002` | error | A project VHDL package named by a use clause was not found in the selected library. |
| `FSIM-ELAB-PKG-003` | error | A selected package constant named by a use clause does not exist. |
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
| `FSIM-ELAB-SVTYPE-001` | error | A SystemVerilog user-defined type is not visible in the unit where it is used. |
| `FSIM-ELAB-SVTYPE-002` | error | The same direct type name is imported from multiple SystemVerilog packages. |
| `FSIM-ELAB-SVTYPE-003` | error | Bounded SystemVerilog typedef aliases contain a cycle. |
| `FSIM-ELAB-SVENUM-001` | error | An enum base width is unsupported or an enumerator value does not fit it. |
| `FSIM-ELAB-SVENUM-002` | error | Two literals in one bounded enum have the same value. |
| `FSIM-ELAB-SVSTRUCT-001` | error | A packed-struct member range or total layout cannot be specialized into a supported width. |
| `FSIM-ELAB-SVSTRUCT-002` | error | A packed-aggregate member read/write has no executable normalized layout. |
| `FSIM-ELAB-SVUNION-001` | error | Packed-union members do not specialize to one common nonzero supported width. |
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
| `FSIM-ELAB-BIND-042` | error | A registered SystemC thread process requires fiber support, but this build configured `FSIM_SYSTEMC_FIBER_MODE=OFF`. |
| `FSIM-ELAB-BIND-043` | error | A SystemC process sensitivity references an unknown registered object. |
| `FSIM-ELAB-BIND-044` | error | A SystemC process registered an invalid sensitivity edge. |
| `FSIM-ELAB-BIND-045` | error | A SystemC edge sensitivity references a non-scalar object. |
| `FSIM-ELAB-BIND-046` | error | A SystemC port/channel binding references an unknown internal signal or aliases one internal signal to conflicting parent signals. |
| `FSIM-ELAB-BIND-047` | error | A constructed native SystemC child has an inconsistent parent handle or direct-child path. |
| `FSIM-ELAB-BIND-048` | error | A typed SystemC export is unbound, cyclic, references an unknown object, or conflicts with another hierarchy alias. |
| `FSIM-ELAB-BIND-049` | error | A packed aggregate crosses a language boundary without a same-language scalar/vector wrapper. |

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
