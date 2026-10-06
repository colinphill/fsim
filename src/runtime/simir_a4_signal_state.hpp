// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/simir_driver_inventory.hpp"
#include "fsim/runtime/simir_region_graph.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace fsim::runtime::simir {

/// Compare the graph structure on which a trusted immutable inventory was
/// built. This does not authenticate a serialized inventory; state ingress
/// validates it by exact comparison with a freshly constructed inventory.
[[nodiscard]] bool signal_driver_inventory_matches_structure(
    const SignalDriverInventory& inventory,
    const RegionGraph& graph) noexcept;

enum class PackedSlotBindingPolicy : std::uint8_t {
    narrow_only,
    /// Versioned owning slots for components admitted by the wide-storage
    /// policy. Checked and unsupported paths remain narrow-only.
    experimental_wide,
    /// Separate opt-in for complete static disjoint-owner partitions. This
    /// policy also permits ordinary wide single-owner slots.
    experimental_wide_disjoint_owners,
};

enum class PackedPlaneRole : std::uint8_t {
    current,
    previous,
    stored,
    owner,
};

struct SignalDriverOwnerLayout {
    ProcessId process { };
    std::size_t first_mask_word { };
    std::size_t first_value_word { };
    std::size_t first_logic9_word { };
    // An unresolved whole-owner signal has no separate raw DriverRecord.
    // Its stored backing is the sole raw owner value as well.
    bool aliases_stored { };
};

struct SignalDriverSignalLayout {
    std::uint32_t width { };
    ValueKind value_kind { ValueKind::logic4 };
    SignalDriverStorageClass storage_class {
        SignalDriverStorageClass::resolved_table
    };
    std::size_t first_value_word { };
    std::size_t first_logic9_word { };
    std::size_t word_count { };
    std::size_t first_owner { };
    std::size_t owner_count { };
};

/// Immutable packed storage layout for one structural component. Only signal
/// IDs explicitly listed by the component are represented. The graph's
/// driver proof selects direct owner storage; resolved or uncertain signals
/// retain their DriverTable route.
class SignalDriverLayout final {
public:
    /// Build the structural owner and bit-mask inventory once for a graph.
    /// Partial projected ranges are retained as candidates and still require
    /// a runtime certificate when a component layout is bound.
    [[nodiscard]] static SignalDriverInventory inventory(
        const RegionGraph& graph);

    [[nodiscard]] static SignalDriverLayout build(
        const RegionGraph& graph,
        std::span<const SignalId> component_signals);

    /// Projected slice signals must be certified from the registered
    /// operation bodies before this overload may classify them as disjoint
    /// owners. All ordinary callers should use the two-argument overload.
    [[nodiscard]] static SignalDriverLayout build(
        const RegionGraph& graph,
        std::span<const SignalId> component_signals,
        std::span<const SignalId> certified_zero_delay_projected_slices);

    /// Full unresolved owners may alias their sole owner value to the
    /// signal's stored plane. Callers must supply only IDs admitted by the
    /// graph/runtime shape proof; this overload rechecks the structural form.
    [[nodiscard]] static SignalDriverLayout build(
        const RegionGraph& graph,
        std::span<const SignalId> component_signals,
        std::span<const SignalId> certified_zero_delay_projected_slices,
        std::span<const SignalId> certified_unresolved_owner_aliases);

    /// Bind a current component to an already-built, trusted structural
    /// inventory. This assigns component-local plane offsets and applies
    /// runtime gates; it never reclassifies the writer set or rebuilds owner
    /// masks. Every indexed inventory span is bounds-checked here.
    [[nodiscard]] static SignalDriverLayout build_from_inventory(
        const RegionGraph& graph,
        std::span<const SignalId> component_signals,
        const SignalDriverInventory& inventory,
        std::span<const SignalId> certified_zero_delay_projected_slices,
        std::span<const SignalId> certified_unresolved_owner_aliases);

    [[nodiscard]] std::size_t signal_count() const noexcept
    {
        return signals_.size();
    }

    [[nodiscard]] std::span<const SignalId> signal_ids() const noexcept
    {
        return signal_ids_;
    }

    [[nodiscard]] std::size_t owner_count() const noexcept
    {
        return owners_.size();
    }

    [[nodiscard]] bool contains(SignalId signal) const noexcept;
    [[nodiscard]] const SignalDriverSignalLayout& signal(
        SignalId signal) const;
    [[nodiscard]] std::span<const SignalDriverOwnerLayout> owners(
        SignalId signal) const;
    [[nodiscard]] std::span<const std::uint64_t> owner_mask_words(
        SignalId signal, ProcessId owner) const noexcept;

    [[nodiscard]] std::size_t value_word_count() const noexcept
    {
        return value_word_count_;
    }

    [[nodiscard]] std::size_t logic9_word_count() const noexcept
    {
        return logic9_word_count_;
    }

    [[nodiscard]] std::size_t owner_value_word_count() const noexcept
    {
        return owner_value_word_count_;
    }

    [[nodiscard]] std::size_t owner_logic9_word_count() const noexcept
    {
        return owner_logic9_word_count_;
    }

    [[nodiscard]] std::size_t max_signal_word_count() const noexcept;

private:
    friend class AuthoritativeSignalPlanes;

    [[nodiscard]] std::size_t signal_index(SignalId signal) const;
    [[nodiscard]] std::size_t owner_index(
        SignalId signal, ProcessId owner) const noexcept;

    // Signal and owner IDs are kept sorted. This sparse mapping avoids a
    // full signal-count-sized side table in every component.
    std::vector<SignalId> signal_ids_;
    std::vector<SignalDriverSignalLayout> signals_;
    std::vector<SignalDriverOwnerLayout> owners_;
    std::vector<std::uint64_t> owner_mask_words_;
    std::size_t value_word_count_ { };
    std::size_t logic9_word_count_ { };
    std::size_t owner_value_word_count_ { };
    std::size_t owner_logic9_word_count_ { };
};

/// Packed current/previous/stored planes and exact S/B raw-owner records.
/// Callers prepare a complete mutation before invoking callbacks; publication
/// then installs only existing words and cannot allocate or throw.
class AuthoritativeSignalPlanes final {
public:
    using ComponentPlaneWordCounts
        = std::array<std::array<std::size_t, 4U>, 4U>;
    using ComponentPlaneSpans
        = std::array<std::array<std::span<std::uint64_t>, 4U>, 4U>;

    struct FrontierWriteBinding {
        SignalId signal { };
        ProcessId owner { };
    };

    /// Temporary exclusive view used by the native region-frontier entry.
    /// The lease locks versioned role blocks or borrows fixed narrow planes
    /// under the state's exclusive lease flag. It is valid only for the
    /// synchronous generated call and must be released before host
    /// publication, observation, or any callback. The owning
    /// AuthoritativeSignalPlanes must outlive the lease.
    class FrontierWriteLease final {
    public:
        FrontierWriteLease() noexcept = default;
        FrontierWriteLease(const FrontierWriteLease&) = delete;
        FrontierWriteLease& operator=(const FrontierWriteLease&) = delete;
        FrontierWriteLease(FrontierWriteLease&& other) noexcept;
        FrontierWriteLease& operator=(FrontierWriteLease&& other) noexcept;
        ~FrontierWriteLease();

        [[nodiscard]] bool active() const noexcept
        {
            return owner_ != nullptr;
        }

        /// Return a read-only current-plane view for any seeded component
        /// signal while current storage is locked or exclusively borrowed.
        [[nodiscard]] bool current_plane_words(SignalId signal,
            std::array<std::span<const std::uint64_t>, 4U>& planes)
            const noexcept;

        /// Return mutable borrowed planes only for one of the certified
        /// whole single-owner output bindings held by this lease. Logic4
        /// returns planes zero and one; Logic9 returns all four state planes.
        /// A caller writing Logic9 must preserve canonical codes zero through
        /// eight and keep unused high tail bits zero, then call
        /// `note_value_change` after the completed write. This borrowed view
        /// does not validate the planes after mutation.
        [[nodiscard]] bool plane_words(SignalId signal,
            PackedPlaneRole role,
            ProcessId owner,
            std::array<std::span<std::uint64_t>, 4U>& planes) const noexcept;

        /// Resolve one binding by its acquisition-time ordinal. The caller
        /// must keep the optional layout-index span passed to acquisition
        /// alive and unchanged until this lease is released.
        [[nodiscard]] bool plane_words_at(std::size_t writable_ordinal,
            SignalId signal,
            PackedPlaneRole role,
            ProcessId owner,
            std::array<std::span<std::uint64_t>, 4U>& planes) const noexcept;

        /// Record one committed signal whose authoritative plane state
        /// changed. Repeated commits may mark the same signal more than once.
        void note_value_change(SignalId signal) noexcept;

        void release() noexcept;

    private:
        friend class AuthoritativeSignalPlanes;

        [[nodiscard]] bool plane_words_for_index(SignalId signal,
            std::size_t signal_index,
            PackedPlaneRole role,
            ProcessId owner,
            std::array<std::span<std::uint64_t>, 4U>& planes)
            const noexcept;

        AuthoritativeSignalPlanes* owner_ { };
        std::span<const FrontierWriteBinding> writable_signals_;
        std::span<const std::size_t> writable_layout_indices_;
        std::array<std::shared_ptr<PackedLogic4PlaneBlock>, 4U> blocks_;
        std::array<PackedLogic4PlaneBlock*, 4U> locked_ { };
        std::uint64_t captured_generation_ { };
        std::size_t lock_count_ { };
        bool versioned_storage_ { };
        bool state_changed_ { };
    };

    struct PreparedWord {
        std::size_t signal_word { };
        std::size_t logic9_word { };
        std::size_t owner_word { };
        std::size_t owner_logic9_word { };
        std::array<std::uint64_t, 4U> old_previous { };
        std::array<std::uint64_t, 4U> old_current { };
        std::array<std::uint64_t, 4U> old_stored { };
        std::array<std::uint64_t, 4U> old_owner { };
        std::array<std::uint64_t, 4U> new_current { };
        std::array<std::uint64_t, 4U> new_stored { };
        std::array<std::uint64_t, 4U> new_owner { };
    };

    struct PreparedMutation {
        /// Captured with the role values used to prepare this mutation.
        /// Publication declines the entire batch if the sidecar has advanced.
        std::uint64_t prepared_generation { };
        SignalId signal { };
        std::size_t signal_index { };
        std::size_t owner_index { };
        std::vector<PreparedWord> words;
        bool any_current_changed { };
        bool any_stored_changed { };
        bool any_owner_changed { };
        bool any_state_changed { };
        bool has_owner { };
        bool owner_is_stored_alias { };
        bool whole_signal { true };
        std::array<std::shared_ptr<PackedLogic4PlaneBlock>, 4U>
            replacements;
        std::array<PackedLogic4PlaneBlock*, 4U> preflight_locked { };
        std::size_t preflight_lock_count { };
        bool preflighted { };
        bool owner_group_row { };
        bool owner_group_preflighted { };
    };

    /// Preallocated full-role copy-on-write capacity for one deferred batch.
    /// The scratch is tied to the exact sidecar instance and is consumed by a
    /// successful publication. False publication attempts leave it reusable.
    struct PreparedGroupScratch final {
        PreparedGroupScratch() noexcept = default;
        PreparedGroupScratch(const PreparedGroupScratch&) = delete;
        PreparedGroupScratch& operator=(const PreparedGroupScratch&) = delete;
        PreparedGroupScratch(PreparedGroupScratch&& other) noexcept;
        PreparedGroupScratch& operator=(PreparedGroupScratch&& other) noexcept;

        [[nodiscard]] bool ready() const noexcept
        {
            return owner_ != nullptr && layout_ != nullptr && !spent_;
        }
        [[nodiscard]] bool spent() const noexcept { return spent_; }

    private:
        friend class AuthoritativeSignalPlanes;

        const AuthoritativeSignalPlanes* owner_ { };
        const SignalDriverLayout* layout_ { };
        std::array<std::shared_ptr<PackedLogic4PlaneBlock>, 4U>
            replacements_;
        bool spent_ { };
    };

    explicit AuthoritativeSignalPlanes(SignalDriverLayout layout,
        PackedSlotBindingPolicy packed_slot_policy
        = PackedSlotBindingPolicy::narrow_only);

    /// The default binds closed single-owner values through one word. The
    /// versioned policy lets admitted components replace pinned role blocks
    /// without invalidating copies or read leases.
    [[nodiscard]] bool supports_packed_slot_binding(
        SignalId signal) const noexcept;
    void stage_packed_signal_slots(SignalId signal,
        PackedLogic4& current,
        PackedLogic4& previous,
        PackedLogic4& stored);
    void stage_packed_owner_slot(
        SignalId signal, ProcessId owner, PackedLogic4& value);
    void stage_packed_owner_stored_alias(
        SignalId signal, ProcessId owner);
    /// These transitions are allocation-free. Callers stage references during
    /// off-side snapshot construction, then bind only in snapshot publication.
    /// Side-effect-free preflight for rebinding the staged slots in place.
    /// This is intended for a serialized quiet point: after every component
    /// passes preflight, `bind_packed_slots()` cannot decline or invalidate a
    /// sidecar before all slot references are installed.
    [[nodiscard]] bool can_bind_packed_slots() const noexcept;
    [[nodiscard]] std::size_t bind_packed_slots() noexcept;
    [[nodiscard]] std::size_t unbind_packed_slots() noexcept;
    [[nodiscard]] bool packed_slots_bound() const noexcept
    {
        return packed_slots_bound_;
    }
    /// True when an ordinary mutation of bound PackedLogic4 roles must first
    /// detach the component slots to protect versioned plane snapshots. The
    /// legacy unversioned narrow binding continues to mirror ordinary writes
    /// in place.
    [[nodiscard]] bool requires_prewrite_unbind() const noexcept
    {
        return versioned_storage_ready_ && packed_slots_bound_;
    }
    [[nodiscard]] bool packed_signal_slots_bound(
        SignalId signal) const noexcept;
    [[nodiscard]] bool packed_owner_slot_bound(
        SignalId signal, ProcessId owner) const noexcept;
    [[nodiscard]] std::size_t packed_slot_count() const noexcept
    {
        return packed_slot_bindings_.size();
    }
    /// Monotonic revision for all current, previous, stored, owner, and
    /// binding transitions in this component.
    [[nodiscard]] std::uint64_t revision() const noexcept
    {
        return generation_;
    }

    /// Initial contents are installed before the state bundle is published.
    /// These methods reject wrong widths, wrong value kinds and duplicate
    /// initialization rather than creating a partially usable plane set.
    void seed_signal(SignalId signal,
        const PackedLogic4& current,
        const PackedLogic4& previous,
        const PackedLogic4& stored);
    void seed_owner(
        SignalId signal, ProcessId owner, const PackedLogic4& value);

    /// Mirrors a runtime publication into already allocated component planes.
    /// These operations are nonthrowing and never change a public source of
    /// truth; callers use them only after their ordinary commit path succeeds.
    void mirror_stored(
        SignalId signal, const PackedLogic4& stored) noexcept;
    void mirror_visible(SignalId signal,
        const PackedLogic4& previous,
        const PackedLogic4& current) noexcept;
    void mirror_owner(
        SignalId signal, ProcessId owner,
        const PackedLogic4& value) noexcept;
    void mirror_owner_into(
        PreparedMutation& mutation,
        SignalId signal,
        ProcessId owner,
        const PackedLogic4& value,
        const PackedLogic4& current,
        const PackedLogic4& stored) noexcept;
    void mirror_logic4_word(SignalId signal,
        Logic4Word previous,
        Logic4Word current,
        Logic4Word stored) noexcept;
    void mirror_logic9_word(SignalId signal,
        Logic9Word previous,
        Logic9Word current,
        Logic9Word stored) noexcept;

    /// Prepare both visible and stored values. A caller that has resolved a
    /// table-backed signal may still mirror its result in the component bank.
    [[nodiscard]] PreparedMutation prepare_value_change(
        SignalId signal,
        const PackedLogic4& current,
        const PackedLogic4& stored) const;

    /// Reuses caller-owned word capacity for a resolved visible/stored update.
    /// Reserve at least the signal word count before entering publication.
    void prepare_value_change_into(
        PreparedMutation& mutation,
        SignalId signal,
        const PackedLogic4& current,
        const PackedLogic4& stored) const;

    /// Prepare a visible/stored publication together with one direct owner's
    /// raw record. `owner_value` is the full-width record after applying the
    /// exact owner write; the layout masks ensure a disjoint owner cannot
    /// overwrite its sibling's raw bits.
    [[nodiscard]] PreparedMutation prepare_owner_change(
        SignalId signal,
        ProcessId owner,
        const PackedLogic4& owner_value,
        const PackedLogic4& current,
        const PackedLogic4& stored) const;

    /// Reuses caller-owned word capacity while preparing a direct-owner
    /// mutation. The caller must reserve at least the signal word count before
    /// entering a fixed-topology publication path.
    void prepare_owner_change_into(
        PreparedMutation& mutation,
        SignalId signal,
        ProcessId owner,
        const PackedLogic4& owner_value,
        const PackedLogic4& current,
        const PackedLogic4& stored) const;

    /// Prepare one row of an owner-group publication without preparing
    /// independent copy-on-write replacements. The group begin method owns
    /// that single whole-span preflight.
    void prepare_owner_group_change_into(
        PreparedMutation& mutation,
        SignalId signal,
        ProcessId owner,
        const PackedLogic4& owner_value,
        const PackedLogic4& current,
        const PackedLogic4& stored) const;

    /// Prepare current/stored from an owner's already mirrored raw planes.
    /// `expected_owner_value` verifies that the resolved staging value still
    /// agrees with the raw owner record without copying that bound record.
    void prepare_owner_value_change_into(
        PreparedMutation& mutation,
        SignalId signal,
        ProcessId owner,
        const PackedLogic4& expected_owner_value) const;

    /// Prepare a static owner slice directly into the reusable word scratch.
    /// The selected range must be fully contained in the owner's proven mask.
    void prepare_owner_slice_change_into(
        PreparedMutation& mutation,
        SignalId signal,
        ProcessId owner,
        const PackedLogic4& slice_value,
        std::size_t offset,
        const PackedLogic4& current,
        const PackedLogic4& stored) const;

    /// Acquires every changed unpinned role or stages a replacement for a
    /// role that already has, or races with, a read lease. It may allocate
    /// while cloning a late-pinned role, but performs no role mutation. If it
    /// returns false, all locks are released and the caller may demote to the
    /// ordinary representation before changing simulation state.
    [[nodiscard]] bool begin_prepared_publication(
        PreparedMutation& mutation);
    void cancel_prepared_publication(
        PreparedMutation& mutation) noexcept;

    /// Preflights one signal's complete set of direct owner mutations. Unlike
    /// publish_group, this permits repeated signal rows only when each row
    /// names a distinct, non-aliased owner and the visible value image agrees
    /// across every row. Publication captures PREVIOUS once and installs all
    /// owner slots with one generation advance.
    [[nodiscard]] bool begin_prepared_owner_group_publication(
        std::span<PreparedMutation> mutations) noexcept;
    void cancel_prepared_owner_group_publication(
        std::span<PreparedMutation> mutations) noexcept;
    [[nodiscard]] bool publish_prepared_owner_group(
        std::span<PreparedMutation> mutations) noexcept;

    /// Installs an already prepared mutation. If current changed, previous is
    /// advanced once to the complete old signal value before current changes.
    /// Sidecar mutation calls require one externally serialized writer.
    /// Snapshot/lease acquisition and reads of already acquired snapshots may
    /// overlap prepared publication; direct live-slot reads and writes may not.
    /// A stale preparation invalidates the sidecar without installing any role.
    void publish(PreparedMutation&& mutation) noexcept;

    /// Installs a complete multi-signal observation phase. All previous
    /// values are captured before any current/stored/owner word changes, and
    /// no callback is invoked until the caller returns from this method.
    /// Mutations must have been prepared already and contain unique signals.
    void publish_group(std::vector<PreparedMutation>&& mutations) noexcept;

    /// Allocate/rearm role-wide COW scratch before deferred mutations become
    /// externally visible. May allocate or throw. On a fixed topology without
    /// retained readers, a spent scratch can recycle its own role buffers.
    void prepare_group_scratch(PreparedGroupScratch& scratch) const;

    /// Validate and publish one row per distinct whole single-owner signal.
    /// Rows may rebase over unrelated component revisions when every captured
    /// old role still matches. False leaves public roles, bindings, validity,
    /// and revision unchanged; true consumes the scratch. No callback or
    /// allocation occurs in this method.
    [[nodiscard]] bool try_rebase_and_publish_group(
        std::span<PreparedMutation> ordered_rows,
        std::uint64_t expected_live_revision,
        PreparedGroupScratch& scratch) noexcept;

    [[nodiscard]] PackedLogic4 current(SignalId signal) const;
    /// Exposes borrowed current aval/bval words only for a valid, seeded
    /// Logic4 signal. The returned spans remain valid only until the next
    /// mirror or publication and cannot be read concurrently with one. Native
    /// callers must also check component generation and compare them with the
    /// captured activation image before frame mutation. Use plane_read_lease
    /// when a read must outlive the immediate serialized access.
    [[nodiscard]] bool current_logic4_planes(SignalId signal,
        std::span<const std::uint64_t>& aval,
        std::span<const std::uint64_t>& bval) const noexcept;
    [[nodiscard]] bool current_planes(SignalId signal,
        std::array<std::span<const std::uint64_t>, 4U>& planes)
        const noexcept;
    [[nodiscard]] PackedLogic4 previous(SignalId signal) const;
    [[nodiscard]] PackedLogic4 stored(SignalId signal) const;
    [[nodiscard]] PackedLogic4 owner_value(
        SignalId signal, ProcessId owner) const;
    [[nodiscard]] PackedLogic4PlaneReadLease plane_read_lease(
        SignalId signal,
        PackedPlaneRole role,
        ProcessId owner = ProcessId { }) const noexcept;

    /// Borrow every value plane during externally serialized access. A live
    /// versioned value loads its backing block once; no read pin is retained.
    /// The spans expire at the next publication, just like aval_words() and
    /// bval_words(). Reborrow after a callback that may publish signal values.
    [[nodiscard]] static std::array<std::span<const std::uint64_t>, 4U>
    borrow_packed_value_planes(const PackedLogic4& value) noexcept;

    /// Copy a wide Logic4 role into a unique owning destination without
    /// allocating or retaining an A4 plane pin. False leaves the destination
    /// unchanged, including when its storage is shared, external, or has a
    /// mismatched shape. Widths through 128 bits use their ordinary inline
    /// value path instead.
    [[nodiscard]] bool try_copy_wide_logic4_role_into(
        SignalId signal,
        PackedPlaneRole role,
        ProcessId owner,
        PackedLogic4& destination) const noexcept;

    /// Acquire the fixed narrow planes or lock versioned role blocks needed
    /// for direct, no-allocation frontier writes. The caller supplies the
    /// exact graph-certified output signal/owner pairs. Any stale generation,
    /// read pin, unsupported kind, unresolved owner shape, or missing slot
    /// declines before mutation. A narrow lease is exclusive and synchronous:
    /// no other state mutation or callback may run until it is released.
    /// The optional layout-index output span may be empty for compatibility.
    /// When supplied, it must have one element per writable binding. A
    /// successful acquisition fills it in binding order and retains the span
    /// until release; the caller must keep it alive and unchanged for that
    /// duration. Its contents are usable only after success.
    [[nodiscard]] bool try_acquire_frontier_write_lease(
        std::uint64_t expected_generation,
        std::span<const FrontierWriteBinding> writable_signals,
        FrontierWriteLease& lease,
        std::span<std::size_t> writable_layout_indices = { }) noexcept;

    [[nodiscard]] const SignalDriverLayout& layout() const noexcept
    {
        return layout_;
    }

    [[nodiscard]] std::span<const std::uint8_t> dirty_signals() const noexcept
    {
        return dirty_signals_;
    }

    /// False after a post-seed Logic9 value uses a reserved plane code.
    /// Native consumers must decline this component and retain checked reads.
    [[nodiscard]] bool valid() const noexcept { return valid_; }

    void clear_dirty() noexcept;

    /// Describe current/previous/stored/raw-owner plane sizes in that role
    /// order. Stage2 uses this before configuring the component allocation.
    [[nodiscard]] ComponentPlaneWordCounts
    component_plane_word_counts() const noexcept;

    /// Move this unpublished state from owning plane vectors into exact arena
    /// slices. The caller must keep every slice within `lifetime`; runtime
    /// preparation verifies that ownership before calling. Existing values
    /// are copied before any descriptor is changed; failure leaves the state
    /// untouched. Snapshots retain the arena through the role blocks' owner.
    [[nodiscard]] bool rehome_component_planes(
        std::shared_ptr<void> lifetime,
        const ComponentPlaneSpans& slices) noexcept;

private:
    struct PlaneWords {
        std::array<PackedLogic4PlaneStorage, 4U> planes;
        std::shared_ptr<PackedLogic4PlaneCell> cell;
        // Retain bounded retired versions for transient snapshot churn. A
        // pinned history stays immutable; further history may still allocate.
        mutable std::array<std::shared_ptr<PackedLogic4PlaneBlock>, 2U>
            retired_blocks;

        [[nodiscard]] const PackedLogic4PlaneStorage& plane(
            std::size_t index) const noexcept
        {
            if (cell) {
                const auto block
                    = cell->current.load(std::memory_order_acquire);
                return block->planes[index];
            }
            return planes[index];
        }

        [[nodiscard]] PackedLogic4PlaneStorage& plane(
            std::size_t index) noexcept
        {
            if (cell) {
                const auto block
                    = cell->current.load(std::memory_order_acquire);
                return block->planes[index];
            }
            return planes[index];
        }
    };

    struct PackedSlotBinding {
        enum class Role : std::uint8_t {
            current,
            previous,
            stored,
            owner
        };

        SignalId signal { };
        ProcessId owner { };
        Role role { Role::current };
        PackedLogic4* value { };
        const PackedLogic4PlaneBacking* backing { };
    };

    struct WritablePlaneTransaction {
        std::array<PackedLogic4PlaneBlock*, 4U> locked { };
        std::array<std::shared_ptr<PackedLogic4PlaneBlock>, 4U>
            replacements;
        std::size_t lock_count { };
    };

    [[nodiscard]] std::array<std::uint64_t, 4U> load_word(
        const PlaneWords& planes,
        std::size_t value_word,
        std::size_t logic9_word,
        ValueKind kind) const noexcept;
    [[nodiscard]] bool matches_value_plane(
        const PlaneWords& planes,
        SignalId signal,
        const PackedLogic4& value) const noexcept;
    void store_word(PlaneWords& planes,
        std::size_t value_word,
        std::size_t logic9_word,
        ValueKind kind,
        const std::array<std::uint64_t, 4U>& value) noexcept;
    void install_previous(const PreparedMutation& mutation) noexcept;
    void install_current_stored_owner(
        const PreparedMutation& mutation) noexcept;
    [[nodiscard]] bool prepare_writable_planes(
        std::span<PreparedMutation> mutations,
        WritablePlaneTransaction& transaction) noexcept;
    [[nodiscard]] bool acquire_prepared_planes(
        PreparedMutation& mutation);
    [[nodiscard]] bool prepared_generation_is_current(
        std::span<const PreparedMutation> mutations) noexcept;
    [[nodiscard]] bool prepared_owner_group_is_valid(
        std::span<const PreparedMutation> mutations) const noexcept;
    [[nodiscard]] bool packed_signal_role_bindings_bound(
        SignalId signal, std::size_t signal_index) const noexcept;
    [[nodiscard]] bool generation_can_advance() const noexcept
    {
        return generation_ != std::numeric_limits<std::uint64_t>::max();
    }
    void advance_generation() noexcept;
    void finish_writable_planes(
        WritablePlaneTransaction& transaction) noexcept;
    void prepare_replacements(PreparedMutation& mutation) const;
    /// Prepare the CURRENT/STORED result of a mirror without materializing
    /// unchanged roles into owning PackedLogic4 temporaries. A null value
    /// selects the corresponding authoritative plane role directly.
    void prepare_value_change_from_roles_into(
        PreparedMutation& mutation,
        SignalId signal,
        const PackedLogic4* current_value,
        const PackedLogic4* stored_value,
        bool prepare_copy_on_write = true) const;
    void prepare_owner_change_from_roles_into(
        PreparedMutation& mutation,
        SignalId signal,
        ProcessId owner,
        const PackedLogic4& owner_value) const;
    void prepare_owner_change_from_prepared_values_into(
        PreparedMutation& mutation,
        SignalId signal,
        ProcessId owner,
        const PackedLogic4& owner_value,
        const PackedLogic4* stored_value,
        bool prepare_copy_on_write) const;
    void apply_replacement_updates(
        PackedLogic4PlaneBlock& block,
        std::span<const PreparedMutation> mutations,
        PackedPlaneRole role) const noexcept;
    [[nodiscard]] PlaneWords& role_words(PackedPlaneRole role) noexcept;
    [[nodiscard]] const PlaneWords& role_words(
        PackedPlaneRole role) const noexcept;
    [[nodiscard]] std::shared_ptr<PackedLogic4PlaneBlock>
    prepare_replacement(const PlaneWords& words) const;
    void install_replacement(PlaneWords& words,
        std::shared_ptr<PackedLogic4PlaneBlock> replacement) noexcept;
    void validate_value(
        SignalId signal, const PackedLogic4& value) const;
    void seed_plane(PlaneWords& planes,
        const SignalDriverSignalLayout& signal,
        const PackedLogic4& value);
    [[nodiscard]] PackedLogic4 materialize(
        const PlaneWords& planes, SignalId signal) const;
    [[nodiscard]] std::size_t owner_index(
        SignalId signal, ProcessId owner) const;
    [[nodiscard]] std::size_t owner_flat_index(
        std::size_t owner_index) const noexcept;

    SignalDriverLayout layout_;
    // Declared before the borrowed planes so it is destroyed after them.
    std::shared_ptr<void> component_storage_owner_;
    PlaneWords current_;
    PlaneWords previous_;
    PlaneWords stored_;
    PlaneWords owner_values_;
    std::vector<PackedLogic4PlaneBacking> current_backings_;
    std::vector<PackedLogic4PlaneBacking> previous_backings_;
    std::vector<PackedLogic4PlaneBacking> stored_backings_;
    std::vector<PackedLogic4PlaneBacking> owner_backings_;
    std::vector<PackedSlotBinding> packed_slot_bindings_;
    // Zero means unstaged; other values store the first role binding index
    // plus one. Role rows are staged together, so the three signal bindings
    // remain a compact indexed range without retaining pointers into the
    // growable binding vector.
    std::vector<std::size_t> packed_signal_slot_binding_tokens_;
    // Zero means unstaged, size_t max marks a staged stored-owner alias, and
    // other values store the packed_slot_bindings_ index plus one. This keeps
    // owner-facade validation constant-time without caching a raw binding
    // pointer across vector growth.
    std::vector<std::size_t> packed_owner_slot_binding_tokens_;
    bool packed_slots_bound_ { };
    std::vector<std::uint8_t> signal_seeded_;
    std::vector<std::uint8_t> owner_seeded_;
    std::vector<std::uint8_t> dirty_signals_;
    // Runtime mirror writes are serialized with other component writers. Keep
    // their per-word preparation storage alive across those writes so a
    // fixed topology does not allocate a temporary mutation for every role.
    PreparedMutation mirror_mutation_scratch_;
    PackedSlotBindingPolicy packed_slot_policy_ {
        PackedSlotBindingPolicy::narrow_only
    };
    bool versioned_storage_ready_ { };
    bool frontier_write_active_ { };
    bool valid_ { true };
    std::uint64_t generation_ { 1U };
};

/// Preallocated readiness and original static-trigger masks for one region.
/// It is a sidecar to the scheduler queue: setting a bit never changes task
/// order or authorizes skipping the interpreter's existing activation proof.
class RegionReadyMask final {
public:
    explicit RegionReadyMask(std::size_t member_count = 0U);

    [[nodiscard]] bool mark(
        std::size_t member, std::uint64_t trigger_mask) noexcept;
    /// Snapshot-certified variant used by generated successor mappings.
    /// The caller supplies the precomputed word and one-bit member mask.
    [[nodiscard]] bool mark_mapped(
        std::size_t member, std::size_t word, std::uint64_t bit,
        std::uint64_t trigger_mask) noexcept;
    [[nodiscard]] bool take_mapped(
        std::size_t member, std::size_t word, std::uint64_t bit,
        std::uint64_t& trigger_mask) noexcept;
    [[nodiscard]] bool ready(std::size_t member) const noexcept;
    [[nodiscard]] std::uint64_t trigger_mask(
        std::size_t member) const noexcept;
    void clear(std::size_t member) noexcept;
    void clear_all() noexcept;

    [[nodiscard]] std::size_t member_count() const noexcept
    {
        return trigger_masks_.size();
    }

private:
    std::vector<std::uint64_t> member_bits_;
    std::vector<std::uint64_t> trigger_masks_;
};

struct RegionFanoutClause {
    EdgeKind edge { EdgeKind::any };
    std::uint32_t offset { };
    std::uint32_t width { };
    std::uint64_t trigger_mask { Process::full_static_trigger_mask };
};

struct RegionFanoutConsumer {
    ProcessId process { };
    std::size_t member_index { };
    std::size_t first_clause { };
    std::size_t clause_count { };
};

struct RegionFanoutSignalGroup {
    SignalId signal { };
    std::size_t first_consumer { };
    std::size_t consumer_count { };
};

/// Component-local fanout grouped by signal and consumer. Matching intervals
/// and edges OR their original static trigger bits once per member.
class RegionGroupedFanout final {
public:
    [[nodiscard]] static RegionGroupedFanout build(
        std::span<const Process* const> programs,
        std::span<const ProcessId> members);

    /// Mark ready component members for a completed signal transition. The
    /// caller continues to enqueue all scheduler work through the ordinary
    /// path; this preallocated sidecar is never a replacement for that queue.
    void mark_transition(SignalId signal,
        const PackedLogic4& previous,
        const PackedLogic4& current,
        EdgeKind edge,
        RegionReadyMask& readiness) const noexcept;
    void mark_transaction(
        SignalId signal, RegionReadyMask& readiness) const noexcept;

private:
    std::vector<RegionFanoutSignalGroup> groups_;
    std::vector<RegionFanoutConsumer> consumers_;
    std::vector<RegionFanoutClause> clauses_;
};

/// Mutable state is owned by one graph component, never by a pooled native
/// backend. A snapshot publishes the component-state vector atomically with
/// its graph/program/backend generation.
class RegionAuthoritativeComponentState final {
public:
    RegionAuthoritativeComponentState(
        std::uint64_t generation,
        SignalDriverLayout layout,
        RegionGroupedFanout fanout,
        std::size_t member_count,
        PackedSlotBindingPolicy packed_slot_policy
            = PackedSlotBindingPolicy::narrow_only);

    [[nodiscard]] std::uint64_t generation() const noexcept
    {
        return generation_;
    }

    [[nodiscard]] AuthoritativeSignalPlanes& values() noexcept
    {
        return values_;
    }

    [[nodiscard]] const AuthoritativeSignalPlanes& values() const noexcept
    {
        return values_;
    }

    [[nodiscard]] RegionReadyMask& readiness() noexcept
    {
        return readiness_;
    }

    [[nodiscard]] bool valid() const noexcept { return values_.valid(); }

    [[nodiscard]] AuthoritativeSignalPlanes::PreparedMutation&
    wide_mutation_scratch() noexcept
    {
        return wide_mutation_scratch_;
    }

    [[nodiscard]] const RegionGroupedFanout& fanout() const noexcept
    {
        return fanout_;
    }

private:
    std::uint64_t generation_ { };
    AuthoritativeSignalPlanes values_;
    RegionGroupedFanout fanout_;
    RegionReadyMask readiness_;
    AuthoritativeSignalPlanes::PreparedMutation wide_mutation_scratch_;
};

} // namespace fsim::runtime::simir
