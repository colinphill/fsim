<!-- SPDX-License-Identifier: Apache-2.0 -->
# Final v1 differential release audit

This is the Batch 130 Task 5 audit of interpreter/LLVM, native-cache,
debugger/VCD, scheduling, callback, and failure evidence. It composes the
language, conformance, and portability inventories rather than replacing their
exact expectation IDs or source provenance.

## Review partitions

| Review ID | Surface | Required check |
|---|---|---|
| `B130-T5-ENGINE` | Interpreter and LLVM O0/O2 | Every claimed compiled path retains interpreter comparison or an exact native-only contract, and optimization is part of cache identity |
| `B130-T5-CACHE` | Cold/warm/edit and selective provenance | Native objects distinguish semantic/source changes, reuse unchanged dependencies, reject malformed artifacts, and never key external runtime contents |
| `B130-T5-DEBUG` | Debugger, source maps, callbacks, normalized VCD | Values, names, hierarchy, safe points, time/delta order, and source identities agree across engines and warm reuse |
| `B130-T5-SCHEDULING` | Active/update/NBA/postponed phases, waits, events, delays, lifecycle, and failures | Deterministic scheduler and failure boundaries remain executable under interpreter and compiled engines |
| `B130-T5-FAILURE` | Diagnostics, invalid HIR/SimIR/native artifacts, callbacks, API/ABI, and plug-ins | Negative paths remain checked, contained, deterministic, and unable to cross a public C boundary as C++ exceptions |

The machine gate composes the final release, SystemVerilog, VHDL,
mixed/SystemC, conformance-corpus, and portability-corpus audits. It then
recounts matrix runtime claims, distinct runtime evidence files, and the union
of named corpus CTests. The corpus union must contain interpreter, LLVM O0/O2,
cold/warm/edit cache, debugger, VCD, callback, diagnostic, lifecycle, ABI,
plug-in, compiler, source-map, and portable-path modes.

The reviewed baseline contains 97 distinct matrix runtime evidence files and
36 distinct corpus CTests. Runtime cells explicitly name interpreter evidence
on 444 rows, LLVM/compiled/native evidence on 362, cache/reuse/edit evidence on
247, debugger evidence on 92, VCD/trace evidence on 119, scheduling/time/event
evidence on 395, and failure/diagnostic/callback/ABI evidence on 90. These
categories overlap intentionally; they freeze claim ownership, not test count.

Task 5 performs no sanitizer, Release, full regression, commit, push, or
GitHub Actions inspection. Those accumulated gates remain Task 10 work.

The review is closed. The 19 unique portability-corpus owners plus the composed
differential gate pass as 20 focused CTests. This includes frontend/runtime,
LLVM O0/O2/cache, SystemC compiler/lifecycle, strict C ABIs, full application,
scoped locals, files/preprocessing, arrays, typed boundaries, public API, and
all static Windows/resource contracts.

Batch 144's ten UDP rows advance the reviewed differential inventory to 1,111
required rows and 103 runtime owners. The linked evidence covers 459
interpreter rows, 378 LLVM rows, 254 cache rows, 97 debugger rows, 127 VCD rows,
410 scheduling rows, and 92 failure rows; the reviewed 36-CTest corpus inventory
is unchanged.

Batch 145's ten strength, switch, and charge rows advance the reviewed
differential inventory to 1,121 required rows and 104 runtime owners. The
linked evidence covers 461 interpreter rows, 380 LLVM rows, 254 cache rows, 98
debugger rows, 128 VCD rows, 411 scheduling rows, and 93 failure rows; the
reviewed 36-CTest corpus inventory remains unchanged.

Batch 146's six specify rows advance the reviewed differential inventory to
1,127 required rows and 107 runtime owners. The linked evidence covers every
module-path delay arity and timing-check family through frontend, elaboration,
runtime, portable artifacts, interpreter, LLVM O0/O2, debugger, callbacks,
VCD, and cache paths. The linked totals are 462 interpreter rows, 381 LLVM
rows, 255 cache rows, 99 debugger rows, 129 VCD rows, 411 scheduling rows, and
94 failure rows. The reviewed 36-CTest corpus adds the dedicated
`fsim.application.specify` owner.
