// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/logic.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

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

/// Four bit-planes carrying the ordinal encoding of up to 64 Logic9 values.
///
/// This is intentionally separate from the aval/bval ABI used by Logic4.
/// Generated code must opt in to this representation rather than silently
/// projecting a nine-state value onto four states.
struct Logic9Word {
    std::size_t width { };
    std::array<std::uint64_t, 4> planes { };

    friend bool operator==(const Logic9Word&, const Logic9Word&) = default;
};

/// A packed two-state vector. Index zero is the rightmost (least-significant)
/// element when constructed from or rendered to a string.
class PackedBit2 {
public:
    explicit PackedBit2(std::size_t width = 0, bool initial = false);

    [[nodiscard]] static PackedBit2 from_msb_string(std::string_view value);

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] bool empty() const noexcept { return width_ == 0; }
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

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] bool empty() const noexcept { return width_ == 0; }
    [[nodiscard]] bool is_logic9() const noexcept { return logic9_; }
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

    [[nodiscard]] std::span<const std::uint64_t>
    aval_words() const noexcept;
    [[nodiscard]] std::span<const std::uint64_t>
    bval_words() const noexcept;
    [[nodiscard]] std::span<const std::uint64_t>
    logic9_plane_words(std::size_t plane) const noexcept;
    [[nodiscard]] std::optional<std::uint64_t>
    known_unsigned_value() const noexcept;
    [[nodiscard]] std::optional<std::int64_t>
    known_signed_value() const noexcept;
    [[nodiscard]] Logic4Word low_word() const;
    /// Replace an existing single-word Logic4 value without constructing a
    /// temporary packed container. The source width must match this value.
    void assign_word(const Logic4Word& source);
    /// Replace an existing single-word Logic9 value without constructing a
    /// temporary packed container. The source width must match this value.
    void assign_logic9_word(const Logic9Word& source);
    /// Replace and compare selected bits of an inline Logic9 value.
    void insert_masked_logic9_word(
        const Logic9Word& source, std::uint64_t mask);
    [[nodiscard]] bool matches_masked_logic9_word(
        const Logic9Word& source, std::uint64_t mask) const;
    /// Return the inline four-state word without validating its domain or width.
    /// Internal execution paths may use this only after independently proving a
    /// nonempty Logic4 width no greater than 64 bits.
    [[nodiscard]] Logic4Word unchecked_low_word() const noexcept
    {
        return { width_, inline_aval_, inline_bval_ };
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

    std::size_t width_ { };
    bool logic9_ { };
    std::uint64_t inline_aval_ { };
    std::uint64_t inline_bval_ { };
    std::uint64_t inline_logic9_plane2_ { };
    std::uint64_t inline_logic9_plane3_ { };
    std::shared_ptr<WideStorage> wide_;
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
