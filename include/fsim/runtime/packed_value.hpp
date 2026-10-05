// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/logic.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::app::application_detail {
class LlvmProcessExecutor;
}

namespace fsim::runtime {

namespace simir {
class AuthoritativeSignalPlanes;
}

/// One role plane backed either by ordinary owning words or by a borrowed
/// component allocation. Copying or moving a borrowed view detaches into
/// owning storage; arena lifetime is pinned separately by the containing role
/// block/state.
class PackedLogic4PlaneStorage final {
public:
    using iterator = std::uint64_t*;
    using const_iterator = const std::uint64_t*;

    PackedLogic4PlaneStorage() noexcept = default;
    PackedLogic4PlaneStorage(const PackedLogic4PlaneStorage& other);
    PackedLogic4PlaneStorage(PackedLogic4PlaneStorage&& other);
    PackedLogic4PlaneStorage& operator=(
        const PackedLogic4PlaneStorage& other);
    PackedLogic4PlaneStorage& operator=(
        PackedLogic4PlaneStorage&& other);

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return size() == 0U; }
    [[nodiscard]] std::uint64_t* data() noexcept;
    [[nodiscard]] const std::uint64_t* data() const noexcept;
    [[nodiscard]] iterator begin() noexcept { return data(); }
    [[nodiscard]] const_iterator begin() const noexcept { return data(); }
    [[nodiscard]] iterator end() noexcept
    {
        auto* const first = data();
        return first == nullptr ? nullptr : first + size();
    }
    [[nodiscard]] const_iterator end() const noexcept
    {
        const auto* const first = data();
        return first == nullptr ? nullptr : first + size();
    }
    [[nodiscard]] std::uint64_t& operator[](std::size_t index) noexcept
    {
        return data()[index];
    }
    [[nodiscard]] const std::uint64_t& operator[](
        std::size_t index) const noexcept
    {
        return data()[index];
    }
    [[nodiscard]] std::uint64_t& front() noexcept { return data()[0U]; }
    [[nodiscard]] const std::uint64_t& front() const noexcept
    {
        return data()[0U];
    }
    [[nodiscard]] std::uint64_t& back() noexcept { return data()[size() - 1U]; }
    [[nodiscard]] const std::uint64_t& back() const noexcept
    {
        return data()[size() - 1U];
    }
    [[nodiscard]] std::span<std::uint64_t> span() noexcept
    {
        return { data(), size() };
    }
    [[nodiscard]] std::span<const std::uint64_t> span() const noexcept
    {
        return { data(), size() };
    }
    void resize(std::size_t size);
    void bind(std::span<std::uint64_t> words) noexcept;
    [[nodiscard]] bool borrowed() const noexcept { return borrowed_storage_; }

private:
    std::vector<std::uint64_t> owned_;
    std::span<std::uint64_t> borrowed_;
    bool borrowed_storage_ { };
};

/// Contiguous storage and explicit read-pin accounting for one A4 plane role.
/// A live component slot owns the current block through a cell; copied packed
/// values and read leases pin a block so it remains immutable and alive.
/// Concurrent readers are supported for pinned snapshots and read leases.
/// Mutating a live slot through spans or scalar/range setters requires
/// external serialization against writers and new snapshot acquisition; this
/// type does not provide general concurrent mutation of a live PackedLogic4.
struct PackedLogic4PlaneBlock final {
    /// Nonempty only when one or more planes borrow a component arena slice.
    /// Snapshots pin the block and therefore keep the backing slab alive.
    std::shared_ptr<void> storage_owner;
    std::array<PackedLogic4PlaneStorage, 4U> planes;

    void acquire_read_pin() noexcept;
    void release_read_pin() noexcept;
    [[nodiscard]] bool has_read_pins() const noexcept;
    [[nodiscard]] bool write_locked() const noexcept;
    [[nodiscard]] bool try_begin_write() noexcept;
    void end_write() noexcept;
    [[nodiscard]] static std::shared_ptr<PackedLogic4PlaneBlock> clone(
        const PackedLogic4PlaneBlock& source);

private:
    static constexpr std::size_t writer_bit
        = std::size_t { 1 }
        << (std::numeric_limits<std::size_t>::digits - 1U);
    std::atomic<std::size_t> reader_state_ { };
};

/// Stable indirection for a role's current version. Readers can capture a
/// block while a prepared mutation atomically installs a replacement.
struct PackedLogic4PlaneCell final {
    explicit PackedLogic4PlaneCell(
        std::shared_ptr<PackedLogic4PlaneBlock> initial) noexcept
        : current(std::move(initial))
    {
    }

    std::atomic<std::shared_ptr<PackedLogic4PlaneBlock>> current;

    [[nodiscard]] std::shared_ptr<PackedLogic4PlaneBlock>
    acquire_read_block() const noexcept;
};

/// Non-owning packed planes used by the SimIR component-backed value slots.
/// The backing descriptor is owned by the component for as long as a
/// PackedLogic4 slot is bound to it. Versioned cells can atomically replace a
/// role's owning plane block while old value snapshots retain the prior one.
/// Logic4 uses planes zero and one, while Logic9 uses all four. `plane_words`
/// returns a mutable borrowed span and requires externally serialized access;
/// concurrent writes and snapshot acquisition must use the sidecar's prepared
/// publication path instead.
struct PackedLogic4PlaneBacking final {
    std::size_t width { };
    bool logic9 { };
    std::size_t first_value_word { };
    std::size_t first_logic9_word { };
    std::shared_ptr<PackedLogic4PlaneCell> cell;
    std::array<std::span<std::uint64_t>, 4U> unversioned_planes;

    [[nodiscard]] std::span<std::uint64_t> plane_words(
        std::size_t plane) const noexcept;
    [[nodiscard]] std::shared_ptr<PackedLogic4PlaneBlock>
    acquire_read_block() const noexcept;
};

/// An owning immutable lease over one contiguous signal value in an A4 plane
/// block. Copies retain an explicit read pin without allocating. A lease may
/// be read concurrently with A4 prepared publication; it never exposes a
/// mutable span.
class PackedLogic4PlaneReadLease final {
public:
    PackedLogic4PlaneReadLease() noexcept = default;
    PackedLogic4PlaneReadLease(const PackedLogic4PlaneReadLease& other) noexcept;
    PackedLogic4PlaneReadLease(PackedLogic4PlaneReadLease&& other) noexcept;
    PackedLogic4PlaneReadLease& operator=(
        const PackedLogic4PlaneReadLease& other) noexcept;
    PackedLogic4PlaneReadLease& operator=(
        PackedLogic4PlaneReadLease&& other) noexcept;
    ~PackedLogic4PlaneReadLease();

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(block_);
    }
    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] bool is_logic9() const noexcept { return logic9_; }
    [[nodiscard]] std::span<const std::uint64_t> plane_words(
        std::size_t plane) const noexcept;

private:
    friend class PackedLogic4;
    friend class simir::AuthoritativeSignalPlanes;

    PackedLogic4PlaneReadLease(
        std::shared_ptr<PackedLogic4PlaneBlock> block,
        std::size_t width,
        bool logic9,
        std::size_t first_value_word,
        std::size_t first_logic9_word) noexcept;
    void reset() noexcept;

    std::shared_ptr<PackedLogic4PlaneBlock> block_;
    std::size_t width_ { };
    bool logic9_ { };
    std::size_t first_value_word_ { };
    std::size_t first_logic9_word_ { };
};

/// The complete aval/bval representation of a four-state value up to 64 bits.
///
/// `width` is part of the value so checked conversion back to PackedLogic4 can
/// preserve masking and reject values outside the single-word fast path.
struct Logic4Word {
    std::size_t width { };
    std::uint64_t aval { };
    std::uint64_t bval { };

    friend bool operator==(const Logic4Word&, const Logic4Word&) = default;
};

/// Allocation-free resolution accumulator for one narrow Logic4 word.
/// Construct it only after the caller has established that every driver is
/// a Logic4 value with this width; Logic9 and wide values use the packed
/// resolution fallback instead.
class Logic4ResolutionAccumulator final {
public:
    /// Width must be in the inclusive range [1, 64].
    explicit Logic4ResolutionAccumulator(std::size_t width);

    /// Add one aval/bval driver with exactly the configured width.
    void add(Logic4Word driver);

    /// Return the resolved value. An accumulator with no drivers resolves to Z.
    [[nodiscard]] Logic4Word result() const noexcept;

private:
    std::size_t width_ { };
    std::uint64_t width_mask_ { };
    std::uint64_t zero_seen_ { };
    std::uint64_t one_seen_ { };
    std::uint64_t unknown_seen_ { };
};

/// Overlay selected bits from `forced` on `driven`, preserving the width and
/// all unselected Logic4 bits. Both words must have a width in [1, 64].
[[nodiscard]] Logic4Word apply_force_word(
    Logic4Word driven,
    const Logic4Word& forced,
    std::uint64_t mask);

/// Four bit-planes carrying the ordinal encoding of up to 64 Logic9 values.
///
/// This is intentionally separate from the aval/bval ABI used by Logic4.
/// Generated code must opt in to this representation rather than silently
/// projecting a nine-state value onto four states. Encodings 9 through 15 are
/// malformed and normalize to Logic9::x at checked value ingress.
struct Logic9Word {
    std::size_t width { };
    std::array<std::uint64_t, 4> planes { };

    /// Canonicalize every malformed four-plane code to the Logic9::x code.
    void normalize_invalid_codes_to_x() noexcept
    {
        const auto invalid
            = planes[3] & (planes[2] | planes[1] | planes[0]);
        planes[0] |= invalid;
        planes[1] &= ~invalid;
        planes[2] &= ~invalid;
        planes[3] &= ~invalid;
    }

    [[nodiscard]] bool has_canonical_codes() const noexcept
    {
        return (planes[3] & (planes[2] | planes[1] | planes[0])) == 0U;
    }

    friend bool operator==(const Logic9Word&, const Logic9Word&) = default;
};

/// A packed two-state vector. Index zero is the rightmost (least-significant)
/// element when constructed from or rendered to a string.
class PackedBit2 {
public:
    explicit PackedBit2(std::size_t width = 0, bool initial = false);

    [[nodiscard]] static PackedBit2 from_msb_string(std::string_view value);

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] bool empty() const noexcept { return width() == 0; }
    [[nodiscard]] bool get(std::size_t index) const;
    void set(std::size_t index, bool value);
    void fill(bool value) noexcept;

    [[nodiscard]] std::span<const std::uint64_t> words() const noexcept
    {
        return words_;
    }
    [[nodiscard]] std::span<std::uint64_t> words() noexcept { return words_; }
    [[nodiscard]] std::string to_msb_string() const;

    friend bool operator==(const PackedBit2&, const PackedBit2&) = default;

private:
    void mask_unused_bits() noexcept;

    std::size_t width_ { };
    std::vector<std::uint64_t> words_;
};

/// A packed four-state vector using the conventional aval/bval encoding:
/// 0=00, 1=10, X=11, Z=01.
/// Ordinary reads and mutations on a live externally backed slot are
/// serialized with its A4 publisher and snapshot acquisition. The copy
/// constructor and plane_read_lease() may acquire an immutable snapshot
/// concurrently with prepared publication; read that snapshot instead of the
/// live slot.
class PackedLogic4 {
public:
    explicit PackedLogic4(std::size_t width = 0,
        Logic4 initial = Logic4::x);
    PackedLogic4(const PackedLogic4&);
    PackedLogic4(PackedLogic4&&) noexcept;
    PackedLogic4& operator=(const PackedLogic4&);
    PackedLogic4& operator=(PackedLogic4&&) noexcept;
    ~PackedLogic4();

    [[nodiscard]] static PackedLogic4 from_msb_string(std::string_view value);
    [[nodiscard]] static PackedLogic4
    from_logic9_msb_string(std::string_view value);
    [[nodiscard]] static PackedLogic4
    from_aval_bval(std::size_t width, std::uint64_t aval,
        std::uint64_t bval);
    [[nodiscard]] static PackedLogic4 from_word_planes(
        std::size_t width,
        std::span<const std::uint64_t> aval,
        std::span<const std::uint64_t> bval);
    [[nodiscard]] static PackedLogic4 from_logic9_word_planes(
        std::size_t width,
        std::span<const std::uint64_t> plane0,
        std::span<const std::uint64_t> plane1,
        std::span<const std::uint64_t> plane2,
        std::span<const std::uint64_t> plane3);
    [[nodiscard]] static PackedLogic4
    from_logic9_word(const Logic9Word& value);

    [[nodiscard]] std::size_t width() const noexcept
    {
        return width_and_logic9_ & ~(logic9_mask | plane_backing_mask);
    }
    [[nodiscard]] bool empty() const noexcept { return width() == 0; }
    [[nodiscard]] bool is_logic9() const noexcept
    {
        return (width_and_logic9_ & logic9_mask) != 0U;
    }
    [[nodiscard]] Logic4 get(std::size_t index) const;
    [[nodiscard]] Logic9 get_logic9(std::size_t index) const;
    void set(std::size_t index, Logic4 value);
    void set_logic9(std::size_t index, Logic9 value);
    /// Replace a contiguous range from the allocation-free single-word ABI.
    void insert_word(const Logic4Word& source, std::size_t offset);
    /// Compare a contiguous range against the allocation-free single-word ABI.
    [[nodiscard]] bool matches_word(
        const Logic4Word& source, std::size_t offset) const;
    /// Replace only bits selected by mask from an allocation-free word.
    void insert_masked_word(
        const Logic4Word& source,
        std::uint64_t mask,
        std::size_t offset);
    /// Compare only bits selected by mask against an allocation-free word.
    [[nodiscard]] bool matches_masked_word(
        const Logic4Word& source,
        std::uint64_t mask,
        std::size_t offset) const;
    /// Replace a contiguous bit range without visiting the unaffected bits.
    void insert_bits(const PackedLogic4& source, std::size_t offset);
    /// Extract a contiguous bit range without visiting individual bits.
    [[nodiscard]] PackedLogic4 extract_bits(
        std::size_t offset, std::size_t width) const;
    void fill(Logic4 value);
    void fill(Logic9 value);

    /// The spans returned by these three accessors are borrowed. For a live
    /// slot they remain valid only until its next publication; callers that
    /// can overlap publication must first make an owning packed snapshot or
    /// retain a PackedLogic4PlaneReadLease.
    [[nodiscard]] std::span<const std::uint64_t>
    aval_words() const noexcept;
    [[nodiscard]] std::span<const std::uint64_t>
    bval_words() const noexcept;
    [[nodiscard]] std::span<const std::uint64_t>
    logic9_plane_words(std::size_t plane) const noexcept;
    /// Returns an owning immutable pin for a live versioned slot or snapshot.
    /// The lease remains stable across A4 prepared publications.
    [[nodiscard]] PackedLogic4PlaneReadLease plane_read_lease() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t>
    known_unsigned_value() const noexcept;
    [[nodiscard]] std::optional<std::int64_t>
    known_signed_value() const noexcept;
    [[nodiscard]] Logic4Word low_word() const;
    /// Replace an existing single-word Logic4 value without constructing a
    /// temporary packed container. The source width must match this value.
    void assign_word(const Logic4Word& source);
    /// Replace an existing single-word Logic9 value without constructing a
    /// temporary packed container. The source width must match this value;
    /// invalid source codes normalize to X as in ordinary Logic9 assignment.
    void assign_logic9_word(const Logic9Word& source);
    /// Replace and compare selected bits of a single-word Logic9 value.
    void insert_masked_logic9_word(
        const Logic9Word& source, std::uint64_t mask);
    [[nodiscard]] bool matches_masked_logic9_word(
        const Logic9Word& source, std::uint64_t mask) const;
    /// Return the inline four-state word without validating its domain or width.
    /// Internal execution paths may use this only after independently proving a
    /// nonempty Logic4 width no greater than 64 bits.
    [[nodiscard]] Logic4Word unchecked_low_word() const noexcept
    {
        if (width() == 0U) {
            return { };
        }
        const auto aval = aval_words();
        const auto bval = bval_words();
        return Logic4Word { width(), aval.front(), bval.front() };
    }
    [[nodiscard]] Logic9Word logic9_low_word() const;
    [[nodiscard]] PackedLogic4 promoted_to_logic9() const;
    [[nodiscard]] std::string to_msb_string() const;

    friend bool operator==(const PackedLogic4&, const PackedLogic4&) noexcept;

private:
    struct WideStorage {
        explicit WideStorage(std::size_t words)
            : aval(words)
            , bval(words)
        {
        }

        std::vector<std::uint64_t> aval;
        std::vector<std::uint64_t> bval;
        std::vector<std::uint64_t> logic9_plane2;
        std::vector<std::uint64_t> logic9_plane3;
    };

    struct InlineStorage {
        std::array<std::uint64_t, 2U> aval { };
        std::array<std::uint64_t, 2U> bval { };
    };

    struct PlaneSnapshotStorage {
        std::shared_ptr<PackedLogic4PlaneBlock> block;
    };

    union ExtraStorage {
        std::shared_ptr<WideStorage> wide;
        const PackedLogic4PlaneBacking* plane_backing;
        PlaneSnapshotStorage plane_snapshot;

        ExtraStorage() noexcept { }
        ~ExtraStorage() noexcept { }
    };

    struct IndirectStorage {
        std::uint64_t first_value_word { };
        std::uint64_t first_logic9_word { };
        ExtraStorage extra;
    };

    union ValueStorage {
        InlineStorage inline_value;
        IndirectStorage indirect;

        ValueStorage() noexcept { }
        ~ValueStorage() noexcept { }
    };
    static_assert(sizeof(ValueStorage) == 4U * sizeof(std::uint64_t));

    // Four inline words hold two Logic4 planes through 128 bits, or four
    // Logic9 planes through 64 bits. External/versioned values keep their
    // owning indirection regardless of whether their width fits inline.
    [[nodiscard]] bool uses_inline_storage() const noexcept
    {
        return width() <= (is_logic9() ? 64U : 128U);
    }
    [[nodiscard]] ExtraStorage& extra_storage() noexcept
    {
        return storage_.indirect.extra;
    }
    [[nodiscard]] const ExtraStorage& extra_storage() const noexcept
    {
        return storage_.indirect.extra;
    }
    [[nodiscard]] std::shared_ptr<WideStorage>& wide_storage()
    {
        return extra_storage().wide;
    }
    [[nodiscard]] const std::shared_ptr<WideStorage>& wide_storage() const
    {
        return extra_storage().wide;
    }
    [[nodiscard]] std::uint64_t& inline_aval() noexcept
    {
        return storage_.inline_value.aval[0U];
    }
    [[nodiscard]] std::uint64_t& inline_bval() noexcept
    {
        return storage_.inline_value.bval[0U];
    }
    [[nodiscard]] std::uint64_t& inline_logic9_plane2() noexcept
    {
        return storage_.inline_value.aval[1U];
    }
    [[nodiscard]] const std::uint64_t& inline_logic9_plane2() const noexcept
    {
        return storage_.inline_value.aval[1U];
    }
    [[nodiscard]] std::uint64_t& inline_logic9_plane3() noexcept
    {
        return storage_.inline_value.bval[1U];
    }
    [[nodiscard]] const std::uint64_t& inline_logic9_plane3() const noexcept
    {
        return storage_.inline_value.bval[1U];
    }
    void initialize_storage_from(const PackedLogic4& other,
        std::shared_ptr<WideStorage> wide) noexcept;

    /// Overwrite an already-owned wide Logic4 value without detaching or
    /// allocating. Returns false without mutation if the value is shared,
    /// externally backed, Logic9, or has an incompatible shape/alias.
    [[nodiscard]] bool try_assign_wide_logic4_word_planes_noalloc(
        std::span<const std::uint64_t> aval,
        std::span<const std::uint64_t> bval) noexcept;

    void set_logic9(const bool enabled) noexcept
    {
        if (enabled) {
            width_and_logic9_ |= logic9_mask;
        } else {
            width_and_logic9_ &= ~logic9_mask;
        }
    }

    void ensure_unique_wide();
    void promote_to_logic9();
    [[nodiscard]] std::span<std::uint64_t>
    mutable_aval_words();
    [[nodiscard]] std::span<std::uint64_t>
    mutable_bval_words();
    [[nodiscard]] std::span<const std::uint64_t>
    logic9_plane(std::size_t index) const noexcept;
    [[nodiscard]] std::span<std::uint64_t>
    mutable_logic9_plane(std::size_t index);
    void mask_unused_bits();

    [[nodiscard]] bool has_plane_backing() const noexcept
    {
        return has_live_plane_backing();
    }
    [[nodiscard]] const PackedLogic4PlaneBacking* plane_backing() const noexcept
    {
        return has_live_plane_backing() ? extra_storage().plane_backing : nullptr;
    }
    [[nodiscard]] bool has_plane_snapshot() const noexcept
    {
        return has_external_planes() && plane_snapshot_;
    }
    [[nodiscard]] bool has_live_plane_backing() const noexcept
    {
        return has_external_planes() && !plane_snapshot_;
    }
    [[nodiscard]] bool has_external_planes() const noexcept
    {
        return (width_and_logic9_ & plane_backing_mask) != 0U;
    }
    [[nodiscard]] std::span<const std::uint64_t>
    snapshot_plane_words(std::size_t plane) const noexcept;
    void capture_plane_snapshot(
        const PackedLogic4PlaneBacking& backing) noexcept;
    [[nodiscard]] std::size_t snapshot_first_value_word() const noexcept
    {
        return static_cast<std::size_t>(storage_.indirect.first_value_word);
    }
    [[nodiscard]] std::size_t snapshot_first_logic9_word() const noexcept
    {
        return static_cast<std::size_t>(storage_.indirect.first_logic9_word);
    }
    void set_snapshot_offsets(std::size_t first_value_word,
        std::size_t first_logic9_word) noexcept
    {
        storage_.indirect.first_value_word = static_cast<std::uint64_t>(first_value_word);
        storage_.indirect.first_logic9_word = static_cast<std::uint64_t>(first_logic9_word);
    }
    void release_plane_snapshot() noexcept;
    void materialize_plane_snapshot();
    void destroy_active_storage() noexcept;
    /// Existing pinned snapshots are detached before a mutable span is
    /// returned, but the caller must serialize access to the live binding
    /// against both sidecar publication and new snapshot/lease acquisition
    /// until it finishes writing through the span.
    [[nodiscard]] std::span<std::uint64_t>
    prepare_plane_storage_for_write(std::size_t plane);
    void bind_plane_backing(
        const PackedLogic4PlaneBacking& backing) noexcept;
    void unbind_plane_backing() noexcept;

    friend class simir::AuthoritativeSignalPlanes;
    friend class fsim::app::application_detail::LlvmProcessExecutor;
    friend class PackedLogic4WideWriteScratchTestAccess;

    static constexpr std::size_t logic9_mask
        = std::size_t { 1 }
        << (std::numeric_limits<std::size_t>::digits - 1U);
    static constexpr std::size_t plane_backing_mask = logic9_mask >> 1U;
    std::size_t width_and_logic9_ { };
    ValueStorage storage_;
    bool plane_snapshot_ { };
};

/// Preferred name for the common packed transport value. PackedLogic4 remains
/// available because the existing public vertical-slice API used that name;
/// exact Logic9 values are distinguished by is_logic9().
using PackedValue = PackedLogic4;

/// A packed std_logic vector encoded as four independent bit planes.
class PackedLogic9 {
public:
    explicit PackedLogic9(std::size_t width = 0,
        Logic9 initial = Logic9::u);

    [[nodiscard]] static PackedLogic9 from_msb_string(std::string_view value);

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] bool empty() const noexcept { return width_ == 0; }
    [[nodiscard]] Logic9 get(std::size_t index) const;
    void set(std::size_t index, Logic9 value);
    void fill(Logic9 value) noexcept;

    [[nodiscard]] std::span<const std::uint64_t>
    plane(std::size_t index) const;
    [[nodiscard]] std::string to_msb_string() const;

    friend bool operator==(const PackedLogic9&, const PackedLogic9&) = default;

private:
    void mask_unused_bits() noexcept;

    std::size_t width_ { };
    std::array<std::vector<std::uint64_t>, 4> planes_;
};

[[nodiscard]] PackedLogic4 collapse_to_logic4(const PackedLogic9& value);
[[nodiscard]] PackedLogic4 collapse_to_logic4(const PackedLogic4& value);
[[nodiscard]] PackedLogic9 expand_to_logic9(const PackedLogic4& value);

[[nodiscard]] PackedLogic4 resolve(std::span<const PackedLogic4> drivers);
[[nodiscard]] PackedLogic9 resolve(std::span<const PackedLogic9> drivers);

} // namespace fsim::runtime
