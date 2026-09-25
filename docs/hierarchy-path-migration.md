<!-- SPDX-License-Identifier: Apache-2.0 -->
# Hierarchy path ownership and artifact migration

Batch 188I replaces repeated C++ hierarchy path ownership with frozen path
tables and table-local IDs. A path spelling remains the identity at public and
foreign interfaces. An ID is useful only together with the table that assigned
it; neither its integer value nor a path string's address is a cross-design
identity.

## C++ owner and ID rules

`fsim::semantic::HierarchyPathTable::Builder` interns `std::string_view`
inputs. `intern()` returns an existing `HierarchyPathId` for the same spelling
and does not require callers to allocate a `std::string` for a lookup.
`std::move(builder).freeze()` consumes the move-only builder and returns a
copyable immutable `HierarchyPathTable`. Copies share storage. `find()` and
`contains()` accept a spelling; `view(id)` checks the ID and returns a borrowed
view. Keep a table owner alive for as long as that view is used. Builder views
remain valid across later `intern()` calls while the builder is alive.

An ID from one table must not index another table, even if both tables contain
the same names. Translate it by spelling:

```cpp
const auto spelling = source.view(source_id);
const auto destination_id = destination.find(spelling);
if (!destination_id) {
    // The destination table cannot represent this path.
}
```

The major C++ migrations are:

| Area | Owner and access | Lifetime rule |
| --- | --- | --- |
| Elaborated runtime | `ElaboratedDesign::hierarchy_paths()`; `find_signal()` and `find_container()` accept spellings | The design owns its frozen table and ID-keyed lookup maps. `rebind_path_table()` accepts an ID-preserving extension. `remap_path_table()` verifies spellings and rekeys the maps when canonical ordering changes IDs. |
| DesignIR | `DesignIr::hierarchy_paths()` and `DesignIr::path(id)`; roots and record paths use `HierarchyPathId` | A `DesignIr` copy or move carries its frozen owner. Retain the design or a copy of its table while using a returned view. `rebind_path_table()` requires the existing ID prefix to keep its meanings. |
| Trace declarations | `TracePathName::view()` on scope, source, variable, and alias declarations | Each `TracePathName` carries a shared frozen table owner and an ID. A detached declaration copy keeps its path alive after the builder, model, or observer is removed. The builder may use source paths and its own fallback table. |
| SystemC inventories | `SystemCKernelChannelInventorySnapshot::paths` and `SystemCKernelBindingInventorySnapshot::paths` | Each snapshot owns the IDs in its entries, including binding chain IDs. Copy or move the complete snapshot rather than retaining an entry ID alone. Descriptor strings remain available to current wire and plugin boundaries. |

The runtime and DesignIR owners can share one immutable table in an enclosing
artifact. A normal live build first freezes runtime paths, extends that table
for DesignIR paths, then uses the ID-preserving `rebind_path_table()` contract.
Artifact decoding can sort the combined path set differently, so it uses
`remap_path_table()` for runtime indexes after decoding and before projection
validation. A caller must not substitute a table by assigning an ID alone.

For example, code that previously kept an owning copy solely to look up a
DesignIR path can borrow the spelling during the design's lifetime:

```cpp
const auto path = design_ir.path(object.path); // borrowed string_view
const auto signal = runtime.find_signal(path);
```

If the spelling must outlive `design_ir`, make the ownership explicit:

```cpp
std::string retained_path { design_ir.path(object.path) };
```

## External string boundaries

Keep owned strings where an external API controls the lifetime or expects a
NUL-terminated name. VPI, VHPI, PLI/ACC, Tcl, debugger, public API, coverage,
trace output, and SystemC plugin C ABI entry points continue to receive path
spellings. The C++ path table does not change any plugin C struct layout or
turn a table-local ID into a foreign handle. Materialize a `std::string` at a
boundary that retains the name; use a temporary view only when the callee
finishes with it before the table owner can be destroyed.

## Persisted tables

Standalone runtime and DesignIR state serializers use their overloads without
an external table. Each writes an inline canonical `FSIMHPT1` path table with
its own numeric path references. The affected component schemas are runtime
64 and DesignIR 5. Their external-table overloads take a frozen
`HierarchyPathTable` before the diagnostics argument.

An enclosing format-14 `.fsimdesign` has one required `hierarchy-paths`
payload at `state/hierarchy-paths.bin`. It is the sorted, unique union of
runtime and DesignIR spellings. The runtime and DesignIR child sections refer
to that table by numeric ID and bind to the SHA-256 digest of its exact
canonical bytes. The loader checks the payload checksum, decodes the bounded
table first, and then supplies that same immutable owner to both child
decoders. A missing table, an invalid reference, a noncanonical ordering, a
duplicate path, or a digest mismatch rejects the artifact. Regenerate older
artifacts with the current compiler; their old schema bytes are not migrated
in place.

An artifact ID is therefore stable only inside the artifact and its bound
table. Do not cache raw numeric IDs across independently built artifacts or
infer semantic equality from matching values. Use spellings for translation
and persistent identity.
