// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fsim::runtime::simir {

enum class SignalDriverInventoryResolution : std::uint8_t {
    none,
    sv_wire,
    std_logic,
    vhdl_user_or,
    vhdl_user_and,
    sv_wand,
    sv_wor,
    sv_user_first,
};

enum class SignalDriverInventoryValueKind : std::uint8_t {
    logic4,
    logic9,
};

enum class SignalDriverInventoryDriverClass : std::uint8_t {
    undriven,
    single_whole,
    single_partial,
    disjoint_partial,
    resolved,
    unknown,
};

enum class SignalDriverInventoryEdge : std::uint8_t {
    any,
    posedge,
    negedge,
    transaction,
};

/// Storage class proven from the immutable writer/access inventory.
enum class SignalDriverStorageClass : std::uint8_t {
    resolved_table,
    single_owner,
    disjoint_owner,
};

/// One statically proven direct owner and its per-signal bit mask. The mask
/// span is structural and independent of any component's packed offsets.
struct SignalDriverInventoryOwner {
    std::uint32_t process { };
    std::size_t first_mask_word { };
    std::size_t mask_word_count { };

    bool operator==(const SignalDriverInventoryOwner&) const = default;
};

/// Stable value copy of a RegionAccess writer row. The public proof header
/// intentionally does not depend on the compiler/runtime graph definitions.
struct SignalDriverInventoryWriter {
    std::uint32_t process { };
    std::uint32_t offset { };
    std::uint32_t width { };
    SignalDriverInventoryEdge edge { SignalDriverInventoryEdge::any };

    bool operator==(const SignalDriverInventoryWriter&) const = default;
};

/// Immutable per-signal input to the runtime S/B/R layout binder. Runtime
/// observations and external-driver capabilities are checked separately and
/// are intentionally absent from this structural record.
struct SignalDriverInventorySignal {
    std::uint32_t width { };
    SignalDriverInventoryResolution resolution {
        SignalDriverInventoryResolution::none
    };
    SignalDriverInventoryValueKind value_kind {
        SignalDriverInventoryValueKind::logic4
    };
    /// Writer-shape class; external-driver capability is checked at runtime.
    SignalDriverInventoryDriverClass drivers {
        SignalDriverInventoryDriverClass::undriven
    };
    bool implicit_driver { };
    bool event_variable { };
    bool writers_unknown { };
    bool dynamic_fork_writers { };
    bool partial_projected_transactions { };
    bool projected_slice_certificate_required { };
    SignalDriverStorageClass storage_class {
        SignalDriverStorageClass::resolved_table
    };
    std::size_t first_writer { };
    std::size_t writer_count { };
    std::size_t first_owner { };
    std::size_t owner_count { };

    bool operator==(const SignalDriverInventorySignal&) const = default;
};

/// Portable elaboration-time S/B/R proof. Owner masks are LSB-first uint64
/// words. The records contain no component-local packed plane offsets; those
/// are assigned when a current runtime component is bound. Serialized values
/// are untrusted: strict artifact loading compares them with a freshly built
/// proof, while the mutable from_state path discards and rebuilds them before
/// a runtime can consume the proof.
struct SignalDriverInventory {
    std::uint32_t version { 1U };
    std::size_t process_count { };
    bool static_access_inventory_complete { true };
    std::vector<SignalDriverInventorySignal> signals;
    std::vector<SignalDriverInventoryWriter> writers;
    std::vector<SignalDriverInventoryOwner> owners;
    std::vector<std::uint64_t> owner_mask_words;

    bool operator==(const SignalDriverInventory&) const = default;
};

} // namespace fsim::runtime::simir
