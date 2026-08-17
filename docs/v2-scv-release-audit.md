<!-- SPDX-License-Identifier: Apache-2.0 -->
# Batch 173 SCV 2.0.1 closure audit

## Scope

Batch 173 integrates the official Accellera SCV 2.0.1 release with fsim's one
governed Accellera SystemC 3.0.2 runtime and native TLM-1/TLM-2 implementation.
Native SCV objects remain inside their owning SystemC island. Only stable,
bounded, pointer-free identities and transaction records cross fsim backend,
artifact, debugger, trace, and loopback boundaries. Worker partitioning,
kernel-per-worker launch, and parallel scheduling remain post-v2.

## Governed upstream source and patch

The authoritative 2017-12-08 archive is 2,835,735 bytes with SHA-256
`7bd1c4037f3c108d02f45cae003d112efdb788d469cb029fada247d330ca4881`.
Its 578-file extracted tree has SHA-256
`85cc2e4e3ee1893878de2567f253003859c847a151f080e48f185825c34f933c`.
The exact Apache-2.0 license, notice, source manifest, SPDX 2.3 package record,
and pristine archive are retained under `third_party/scv-2.0.1`.

An unmodified pre-C++20 GCC 13 build against SystemC 3.0.2 is retained before
patching. Clang 22 correctly rejects lazy assignment through the const
`scv_bag::peekRandom()` path, and GCC 13 in the required C++20 mode rejects the
template-id spelling of constructors in the nested `scv_extensions`
specialization. The isolated build tree therefore receives two generated all-
platform patches: one makes only the cached random-generator pointer mutable,
and one uses the injected class name for those two constructors. `PATCHES.txt`
freezes both patch/input/output identities, the complete patched-tree digest,
rationales, probes and independent removal criteria. Remove either patch only
when a governed official SCV release corrects its corresponding source defect.

## Supported boundary

The installed `SCV::scv` shared library, headers, CMake metadata and pkg-config
metadata resolve the same governed SystemC runtime after relocation. Producer
identity covers SCV source/patch, SystemC/TLM, compiler and standard library,
adapter/plug-in ABI, artifact schema and cache schema. Source plug-ins,
incremental objects, mapped libraries and standalone designs reject stale or
corrupt producers before loading native payloads.

The schema-1 backend and transport codecs carry stable island, hierarchy,
object, stream, generator, transaction and sequence identities. Deterministic
randomization, native smart-pointer ownership, constraints/distributions,
extension introspection, common transaction records, native callback recording,
SystemC/TLM/VCD/FST correlation, direct/loopback replay and deterministic merge
are covered by the eighteen-row inventory. The official corpus uses four
unmodified upstream examples; target-local warning compatibility does not alter
upstream source or project-wide warnings.

## Resource baseline

The bounded Debug probe records 64 transactions with three user attributes
each. Repeated runs publish exactly 32,448 canonical bytes with SHA-256
`08de48315d224e3a9340adecaec8c3bb747dc84b43b1a08e6c23a99d264f42a2`.
A four-record queue peaks at 2,028 bytes and reports fifteen recoverable
backpressure events. A separate byte-limited queue produces the same output.
Disabled recording still measures native callback work but publishes zero
records and zero serialized bytes. Invalid-producer and disconnected-consumer
injections recover without leaked handles, queue mutation, loss, duplication,
or reordered output. A contradictory native constraint solve reaches its
explicit one-step ceiling without changing the target.

## Evidence and final-release deferral

The normalized eighteen-row inventory is fully preserved with zero active rows
at SHA-256
`78fda3ccb3264ad3833ddb7334f1b49cdd3c63970fb7aaacd32b8c8b6ad997b0`.
The registered closure uses fixture witnesses during an ordinary regression so
the underlying SCV tests execute once; direct closure invocation retains four
verbose stage logs and a result ledger. The direct focused Debug closure passes
33/33 in 10.08 seconds. Its fixture-driven registered form passes 31/31 in
14.97 seconds with the nonrecursive driver taking 0.01 seconds. Batch 173
qualification is intentionally Debug-only. Release, sanitizer, and hosted CI
execution and repair are deferred to final release Batch 177, which must cover
both Linux and Windows before v2 release closure.
