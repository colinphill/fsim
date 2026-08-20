// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/packed_value.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace fsim::runtime {
namespace {

    constexpr std::size_t bits_per_word = 64;

    [[nodiscard]] std::size_t word_count(std::size_t width)
    {
        if (width > std::numeric_limits<std::size_t>::max() - (bits_per_word - 1)) {
            throw std::length_error("packed value width is too large");
        }
        return (width + bits_per_word - 1) / bits_per_word;
    }

    void check_index(std::size_t index, std::size_t width)
    {
        if (index >= width) {
            throw std::out_of_range("packed value bit index is out of range");
        }
    }

    [[nodiscard]] constexpr std::uint64_t final_word_mask(std::size_t width)
    {
        const auto remainder = width % bits_per_word;
        return remainder == 0 ? ~std::uint64_t { 0 }
                              : (std::uint64_t { 1 } << remainder) - 1;
    }

    void mask_last(std::span<std::uint64_t> words, std::size_t width) noexcept
    {
        if (!words.empty()) {
            words.back() &= final_word_mask(width);
        }
    }

    [[nodiscard]] bool read_bit(std::span<const std::uint64_t> words,
        std::size_t index) noexcept
    {
        return ((words[index / bits_per_word] >> (index % bits_per_word)) & 1U) != 0;
    }

    void write_bit(std::span<std::uint64_t> words, std::size_t index,
        bool value) noexcept
    {
        const auto mask = std::uint64_t { 1 } << (index % bits_per_word);
        auto& word = words[index / bits_per_word];
        if (value) {
            word |= mask;
        } else {
            word &= ~mask;
        }
    }

    void copy_bit_range(std::span<std::uint64_t> destination,
        const std::span<const std::uint64_t> source,
        const std::size_t destination_offset,
        const std::size_t width) noexcept
    {
        const auto range_end = destination_offset + width;
        const auto first_word = destination_offset / bits_per_word;
        const auto last_word = (range_end - 1U) / bits_per_word;
        for (auto word = first_word; word <= last_word; ++word) {
            const auto destination_begin = word * bits_per_word;
            const auto copied_begin = std::max(destination_begin, destination_offset);
            const auto copied_end = std::min(destination_begin + bits_per_word,
                range_end);
            const auto copied_width = copied_end - copied_begin;
            const auto source_begin = copied_begin - destination_offset;
            const auto source_word = source_begin / bits_per_word;
            const auto source_shift = source_begin % bits_per_word;
            auto bits = source[source_word] >> source_shift;
            if (source_shift != 0U && source_word + 1U < source.size()) {
                bits |= source[source_word + 1U] << (bits_per_word - source_shift);
            }
            const auto copied_mask = copied_width == bits_per_word
                ? ~std::uint64_t { 0 }
                : (std::uint64_t { 1 } << copied_width) - 1U;
            const auto destination_shift = copied_begin - destination_begin;
            const auto destination_mask = copied_mask << destination_shift;
            destination[word] = (destination[word] & ~destination_mask)
                | ((bits & copied_mask) << destination_shift);
        }
    }

    void extract_bit_range(
        const std::span<const std::uint64_t> source,
        const std::size_t source_offset,
        const std::span<std::uint64_t> destination) noexcept
    {
        const auto source_word = source_offset / bits_per_word;
        const auto source_shift = source_offset % bits_per_word;
        for (std::size_t word = 0; word < destination.size(); ++word) {
            const auto index = source_word + word;
            auto bits = source[index] >> source_shift;
            if (source_shift != 0U && index + 1U < source.size()) {
                bits |= source[index + 1U] << (bits_per_word - source_shift);
            }
            destination[word] = bits;
        }
    }

    void copy_word_range(
        const std::span<std::uint64_t> destination,
        std::uint64_t source,
        const std::size_t destination_offset,
        const std::size_t width) noexcept
    {
        const auto source_mask = final_word_mask(width);
        source &= source_mask;
        const auto first_word = destination_offset / bits_per_word;
        const auto shift = destination_offset % bits_per_word;
        if (shift == 0U) {
            destination[first_word]
                = (destination[first_word] & ~source_mask) | source;
            return;
        }

        const auto first_width = std::min(width, bits_per_word - shift);
        const auto first_mask = final_word_mask(first_width) << shift;
        destination[first_word]
            = (destination[first_word] & ~first_mask)
            | ((source << shift) & first_mask);
        if (first_width == width) {
            return;
        }
        const auto second_width = width - first_width;
        const auto second_mask = final_word_mask(second_width);
        destination[first_word + 1U]
            = (destination[first_word + 1U] & ~second_mask)
            | ((source >> first_width) & second_mask);
    }

    [[nodiscard]] std::uint64_t read_word_range(
        const std::span<const std::uint64_t> source,
        const std::size_t source_offset,
        const std::size_t width) noexcept
    {
        const auto first_word = source_offset / bits_per_word;
        const auto shift = source_offset % bits_per_word;
        auto result = source[first_word] >> shift;
        if (shift != 0U && shift + width > bits_per_word) {
            result |= source[first_word + 1U] << (bits_per_word - shift);
        }
        return result & final_word_mask(width);
    }

} // namespace

PackedBit2::PackedBit2(std::size_t width, bool initial)
    : width_(width)
    , words_(word_count(width), initial ? ~std::uint64_t { 0 } : 0)
{
    mask_unused_bits();
}

PackedBit2 PackedBit2::from_msb_string(std::string_view value)
{
    PackedBit2 result(value.size());
    for (std::size_t offset = 0; offset < value.size(); ++offset) {
        const auto character = value[value.size() - offset - 1];
        if (character != '0' && character != '1') {
            throw std::invalid_argument("two-state vector contains a non-binary digit");
        }
        result.set(offset, character == '1');
    }
    return result;
}

bool PackedBit2::get(std::size_t index) const
{
    check_index(index, width_);
    return read_bit(words_, index);
}

void PackedBit2::set(std::size_t index, bool value)
{
    check_index(index, width_);
    write_bit(words_, index, value);
}

void PackedBit2::fill(bool value) noexcept
{
    std::fill(words_.begin(), words_.end(),
        value ? ~std::uint64_t { 0 } : std::uint64_t { 0 });
    mask_unused_bits();
}

std::string PackedBit2::to_msb_string() const
{
    std::string result(width_, '0');
    for (std::size_t index = 0; index < width_; ++index) {
        result[width_ - index - 1] = get(index) ? '1' : '0';
    }
    return result;
}

void PackedBit2::mask_unused_bits() noexcept { mask_last(words_, width_); }

PackedLogic4::PackedLogic4(std::size_t width, Logic4 initial)
    : width_(width)
    , wide_(width > bits_per_word
              ? std::make_shared<WideStorage>(word_count(width))
              : nullptr)
{
    fill(initial);
}

PackedLogic4::PackedLogic4(const PackedLogic4&) = default;
PackedLogic4::PackedLogic4(PackedLogic4&&) noexcept = default;
PackedLogic4& PackedLogic4::operator=(const PackedLogic4&) = default;
PackedLogic4& PackedLogic4::operator=(PackedLogic4&&) noexcept = default;
PackedLogic4::~PackedLogic4() = default;

void PackedLogic4::ensure_unique_wide()
{
    if (width_ <= bits_per_word) {
        return;
    }
    if (!wide_) {
        wide_ = std::make_shared<WideStorage>(word_count(width_));
        return;
    }
    if (wide_.unique()) {
        return;
    }
    wide_ = std::make_shared<WideStorage>(*wide_);
}

bool operator==(
    const PackedLogic4& left,
    const PackedLogic4& right) noexcept
{
    if (&left == &right) {
        return true;
    }
    if (left.width_ != right.width_ || left.logic9_ != right.logic9_) {
        return false;
    }
    if (left.width_ <= bits_per_word) {
        return left.inline_aval_ == right.inline_aval_
            && left.inline_bval_ == right.inline_bval_
            && (!left.logic9_
                || (left.inline_logic9_plane2_
                        == right.inline_logic9_plane2_
                    && left.inline_logic9_plane3_
                        == right.inline_logic9_plane3_));
    }
    if (left.wide_ == right.wide_) {
        return true;
    }
    return left.wide_->aval == right.wide_->aval
        && left.wide_->bval == right.wide_->bval
        && (!left.logic9_
            || (left.wide_->logic9_plane2 == right.wide_->logic9_plane2
                && left.wide_->logic9_plane3
                    == right.wide_->logic9_plane3));
}

PackedLogic4 PackedLogic4::from_msb_string(std::string_view value)
{
    PackedLogic4 result(value.size(), Logic4::zero);
    for (std::size_t offset = 0; offset < value.size(); ++offset) {
        const auto parsed = parse_logic4(value[value.size() - offset - 1]);
        if (!parsed) {
            throw std::invalid_argument("four-state vector contains an invalid digit");
        }
        result.set(offset, *parsed);
    }
    return result;
}

PackedLogic4 PackedLogic4::from_logic9_msb_string(
    const std::string_view value)
{
    PackedLogic4 result(value.size(), Logic4::zero);
    result.promote_to_logic9();
    for (std::size_t offset = 0; offset < value.size(); ++offset) {
        const auto parsed = parse_logic9(value[value.size() - offset - 1]);
        if (!parsed) {
            throw std::invalid_argument(
                "nine-state vector contains an invalid digit");
        }
        result.set_logic9(offset, *parsed);
    }
    return result;
}

PackedLogic4 PackedLogic4::from_aval_bval(
    const std::size_t width,
    const std::uint64_t aval,
    const std::uint64_t bval)
{
    if (width == 0 || width > bits_per_word) {
        throw std::invalid_argument(
            "four-state word width must be between 1 and 64");
    }
    PackedLogic4 result(width, Logic4::zero);
    result.inline_aval_ = aval;
    result.inline_bval_ = bval;
    result.mask_unused_bits();
    return result;
}

PackedLogic4 PackedLogic4::from_word_planes(
    const std::size_t width,
    const std::span<const std::uint64_t> aval,
    const std::span<const std::uint64_t> bval)
{
    const auto words = word_count(width);
    if (width == 0 || aval.size() != words || bval.size() != words) {
        throw std::invalid_argument(
            "four-state plane dimensions do not match the packed width");
    }
    PackedLogic4 result(width, Logic4::zero);
    std::ranges::copy(aval, result.mutable_aval_words().begin());
    std::ranges::copy(bval, result.mutable_bval_words().begin());
    result.mask_unused_bits();
    return result;
}

PackedLogic4 PackedLogic4::from_logic9_word_planes(
    const std::size_t width,
    const std::span<const std::uint64_t> plane0,
    const std::span<const std::uint64_t> plane1,
    const std::span<const std::uint64_t> plane2,
    const std::span<const std::uint64_t> plane3)
{
    const auto words = word_count(width);
    if (width == 0 || plane0.size() != words || plane1.size() != words
        || plane2.size() != words || plane3.size() != words) {
        throw std::invalid_argument(
            "nine-state plane dimensions do not match the packed width");
    }
    PackedLogic4 result(width, Logic4::zero);
    result.logic9_ = true;
    if (width > bits_per_word) {
        result.wide_->logic9_plane2.resize(words);
        result.wide_->logic9_plane3.resize(words);
    }
    std::ranges::copy(plane0, result.mutable_logic9_plane(0).begin());
    std::ranges::copy(plane1, result.mutable_logic9_plane(1).begin());
    std::ranges::copy(plane2, result.mutable_logic9_plane(2).begin());
    std::ranges::copy(plane3, result.mutable_logic9_plane(3).begin());
    result.mask_unused_bits();
    return result;
}

PackedLogic4 PackedLogic4::from_logic9_word(
    const Logic9Word& value)
{
    if (value.width == 0 || value.width > bits_per_word) {
        throw std::invalid_argument(
            "nine-state word width must be between 1 and 64");
    }
    PackedLogic4 result(value.width, Logic4::zero);
    result.logic9_ = true;
    result.inline_aval_ = value.planes[0];
    result.inline_bval_ = value.planes[1];
    result.inline_logic9_plane2_ = value.planes[2];
    result.inline_logic9_plane3_ = value.planes[3];
    result.mask_unused_bits();
    return result;
}

void PackedLogic4::assign_logic9_word(const Logic9Word& source)
{
    if (!logic9_ || width_ == 0U || width_ > bits_per_word
        || source.width != width_) {
        throw std::invalid_argument(
            "nine-state word assignment requires a matching inline value");
    }
    inline_aval_ = source.planes[0];
    inline_bval_ = source.planes[1];
    inline_logic9_plane2_ = source.planes[2];
    inline_logic9_plane3_ = source.planes[3];
    mask_unused_bits();
}

void PackedLogic4::insert_masked_logic9_word(
    const Logic9Word& source,
    std::uint64_t mask)
{
    if (!logic9_ || width_ == 0U || width_ > bits_per_word
        || source.width != width_) {
        throw std::invalid_argument(
            "masked nine-state word assignment requires a matching inline value");
    }
    mask &= final_word_mask(width_);
    inline_aval_ = (inline_aval_ & ~mask) | (source.planes[0] & mask);
    inline_bval_ = (inline_bval_ & ~mask) | (source.planes[1] & mask);
    inline_logic9_plane2_ = (inline_logic9_plane2_ & ~mask)
        | (source.planes[2] & mask);
    inline_logic9_plane3_ = (inline_logic9_plane3_ & ~mask)
        | (source.planes[3] & mask);
}

bool PackedLogic4::matches_masked_logic9_word(
    const Logic9Word& source,
    std::uint64_t mask) const
{
    if (!logic9_ || width_ == 0U || width_ > bits_per_word
        || source.width != width_) {
        return false;
    }
    mask &= final_word_mask(width_);
    return (((inline_aval_ ^ source.planes[0]) & mask) == 0U)
        && (((inline_bval_ ^ source.planes[1]) & mask) == 0U)
        && (((inline_logic9_plane2_ ^ source.planes[2]) & mask) == 0U)
        && (((inline_logic9_plane3_ ^ source.planes[3]) & mask) == 0U);
}

std::span<const std::uint64_t>
PackedLogic4::aval_words() const noexcept
{
    if (width_ == 0) {
        return { };
    }
    if (width_ <= bits_per_word) {
        return { &inline_aval_, 1 };
    }
    return wide_->aval;
}

std::span<const std::uint64_t>
PackedLogic4::bval_words() const noexcept
{
    if (width_ == 0) {
        return { };
    }
    if (width_ <= bits_per_word) {
        return { &inline_bval_, 1 };
    }
    return wide_->bval;
}

std::span<const std::uint64_t>
PackedLogic4::logic9_plane_words(const std::size_t plane) const noexcept
{
    if (!logic9_ || plane >= 4) {
        return { };
    }
    return logic9_plane(plane);
}

std::span<std::uint64_t>
PackedLogic4::mutable_aval_words()
{
    if (width_ == 0) {
        return { };
    }
    if (width_ <= bits_per_word) {
        return { &inline_aval_, 1 };
    }
    ensure_unique_wide();
    return wide_->aval;
}

std::span<std::uint64_t>
PackedLogic4::mutable_bval_words()
{
    if (width_ == 0) {
        return { };
    }
    if (width_ <= bits_per_word) {
        return { &inline_bval_, 1 };
    }
    ensure_unique_wide();
    return wide_->bval;
}

std::optional<std::uint64_t>
PackedLogic4::known_unsigned_value() const noexcept
{
    if (width_ == 0 || logic9_) {
        return std::nullopt;
    }
    const auto aval = aval_words();
    const auto bval = bval_words();
    if (std::ranges::any_of(bval, [](const auto word) { return word != 0; })
        || std::ranges::any_of(
            aval.subspan(1), [](const auto word) { return word != 0; })) {
        return std::nullopt;
    }
    return aval.front();
}

std::optional<std::int64_t>
PackedLogic4::known_signed_value() const noexcept
{
    if (width_ == 0 || logic9_) {
        return std::nullopt;
    }
    const auto aval = aval_words();
    const auto bval = bval_words();
    if (std::ranges::any_of(bval, [](const auto word) { return word != 0; })) {
        return std::nullopt;
    }

    auto low = aval.front();
    if (width_ < bits_per_word) {
        const auto sign = UINT64_C(1) << (width_ - 1U);
        if ((low & sign) != 0) {
            low |= ~final_word_mask(width_);
        }
        return std::bit_cast<std::int64_t>(low);
    }

    const auto negative = (low & (UINT64_C(1) << 63U)) != 0;
    for (std::size_t index = 1; index < aval.size(); ++index) {
        const auto mask = index + 1U == aval.size()
            ? final_word_mask(width_)
            : std::numeric_limits<std::uint64_t>::max();
        const auto extension = negative ? mask : UINT64_C(0);
        if ((aval[index] & mask) != extension) {
            return std::nullopt;
        }
    }
    return std::bit_cast<std::int64_t>(low);
}

Logic4Word PackedLogic4::low_word() const
{
    if (width_ == 0 || width_ > bits_per_word) {
        throw std::invalid_argument(
            "four-state word width must be between 1 and 64");
    }
    if (logic9_) {
        throw std::invalid_argument(
            "an exact nine-state value has no lossless aval/bval word");
    }
    return { width_, inline_aval_, inline_bval_ };
}

void PackedLogic4::assign_word(const Logic4Word& source)
{
    if (logic9_ || width_ == 0U || width_ > bits_per_word
        || source.width != width_) {
        throw std::invalid_argument(
            "assigned four-state word must match a single-word Logic4 value");
    }
    const auto mask = final_word_mask(width_);
    inline_aval_ = source.aval & mask;
    inline_bval_ = source.bval & mask;
}

Logic4 PackedLogic4::get(std::size_t index) const
{
    check_index(index, width_);
    if (logic9_) {
        return to_logic4(get_logic9(index));
    }
    const auto aval = read_bit(aval_words(), index);
    const auto bval = read_bit(bval_words(), index);
    if (!bval) {
        return aval ? Logic4::one : Logic4::zero;
    }
    return aval ? Logic4::x : Logic4::z;
}

Logic9 PackedLogic4::get_logic9(const std::size_t index) const
{
    check_index(index, width_);
    if (!logic9_) {
        return to_logic9(get(index));
    }
    std::uint8_t encoded { };
    for (std::size_t plane = 0; plane < 4; ++plane) {
        encoded = static_cast<std::uint8_t>(
            encoded
            | (static_cast<std::uint8_t>(
                   read_bit(logic9_plane(plane), index))
                << plane));
    }
    return encoded
            <= static_cast<std::uint8_t>(Logic9::dont_care)
        ? static_cast<Logic9>(encoded)
        : Logic9::x;
}

void PackedLogic4::set(std::size_t index, Logic4 value)
{
    check_index(index, width_);
    if (logic9_) {
        set_logic9(index, to_logic9(value));
        return;
    }
    const auto aval = mutable_aval_words();
    const auto bval = mutable_bval_words();
    switch (value) {
    case Logic4::zero:
        write_bit(aval, index, false);
        write_bit(bval, index, false);
        break;
    case Logic4::one:
        write_bit(aval, index, true);
        write_bit(bval, index, false);
        break;
    case Logic4::x:
        write_bit(aval, index, true);
        write_bit(bval, index, true);
        break;
    case Logic4::z:
        write_bit(aval, index, false);
        write_bit(bval, index, true);
        break;
    }
}

void PackedLogic4::set_logic9(
    const std::size_t index,
    const Logic9 value)
{
    check_index(index, width_);
    if (!logic9_) {
        promote_to_logic9();
    }
    const auto encoded = static_cast<std::uint8_t>(value);
    for (std::size_t plane = 0; plane < 4; ++plane) {
        write_bit(
            mutable_logic9_plane(plane),
            index,
            ((encoded >> plane) & 1U) != 0);
    }
}

void PackedLogic4::insert_word(
    const Logic4Word& source,
    const std::size_t offset)
{
    if (source.width == 0U || source.width > bits_per_word
        || offset > width_ || source.width > width_ - offset) {
        throw std::invalid_argument(
            "insert word range is outside its target value");
    }
    if (!logic9_) {
        copy_word_range(
            mutable_aval_words(), source.aval, offset, source.width);
        copy_word_range(
            mutable_bval_words(), source.bval, offset, source.width);
        mask_unused_bits();
        return;
    }

    const auto mask = final_word_mask(source.width);
    const auto aval = source.aval & mask;
    const auto bval = source.bval & mask;
    copy_word_range(
        mutable_logic9_plane(0U), aval, offset, source.width);
    copy_word_range(
        mutable_logic9_plane(1U), ~bval, offset, source.width);
    copy_word_range(
        mutable_logic9_plane(2U), ~aval & bval, offset, source.width);
    copy_word_range(
        mutable_logic9_plane(3U), 0U, offset, source.width);
    mask_unused_bits();
}

bool PackedLogic4::matches_word(
    const Logic4Word& source,
    const std::size_t offset) const
{
    if (source.width == 0U || source.width > bits_per_word
        || offset > width_ || source.width > width_ - offset) {
        throw std::invalid_argument(
            "word comparison range is outside its target value");
    }
    if (logic9_) {
        return false;
    }
    const auto mask = final_word_mask(source.width);
    if (width_ <= bits_per_word) {
        return ((inline_aval_ >> offset) & mask) == (source.aval & mask)
            && ((inline_bval_ >> offset) & mask) == (source.bval & mask);
    }
    return read_word_range(aval_words(), offset, source.width)
        == (source.aval & mask)
        && read_word_range(bval_words(), offset, source.width)
        == (source.bval & mask);
}

void PackedLogic4::insert_masked_word(
    const Logic4Word& source,
    std::uint64_t mask,
    const std::size_t offset)
{
    if (source.width == 0U || source.width > bits_per_word
        || offset > width_ || source.width > width_ - offset) {
        throw std::invalid_argument(
            "masked insert word range is outside its target value");
    }
    const auto width_mask = final_word_mask(source.width);
    mask &= width_mask;
    if (mask == 0U) {
        return;
    }
    if (mask == width_mask) {
        insert_word(source, offset);
        return;
    }
    if (logic9_) {
        for (std::size_t bit = 0; bit < source.width; ++bit) {
            if (((mask >> bit) & UINT64_C(1)) == 0U) {
                continue;
            }
            const auto aval = ((source.aval >> bit) & UINT64_C(1)) != 0U;
            const auto bval = ((source.bval >> bit) & UINT64_C(1)) != 0U;
            set(offset + bit,
                !bval ? (aval ? Logic4::one : Logic4::zero)
                      : (aval ? Logic4::x : Logic4::z));
        }
        return;
    }

    if ((offset % bits_per_word) == 0U) {
        const auto word = offset / bits_per_word;
        auto aval = mutable_aval_words();
        auto bval = mutable_bval_words();
        aval[word] = (aval[word] & ~mask) | (source.aval & mask);
        bval[word] = (bval[word] & ~mask) | (source.bval & mask);
        mask_unused_bits();
        return;
    }

    const auto current_aval = read_word_range(
        aval_words(), offset, source.width);
    const auto current_bval = read_word_range(
        bval_words(), offset, source.width);
    copy_word_range(
        mutable_aval_words(),
        (current_aval & ~mask) | (source.aval & mask),
        offset,
        source.width);
    copy_word_range(
        mutable_bval_words(),
        (current_bval & ~mask) | (source.bval & mask),
        offset,
        source.width);
    mask_unused_bits();
}

bool PackedLogic4::matches_masked_word(
    const Logic4Word& source,
    std::uint64_t mask,
    const std::size_t offset) const
{
    if (source.width == 0U || source.width > bits_per_word
        || offset > width_ || source.width > width_ - offset) {
        throw std::invalid_argument(
            "masked word comparison range is outside its target value");
    }
    if (logic9_) {
        return false;
    }
    mask &= final_word_mask(source.width);
    if (mask == 0U) {
        return true;
    }
    if ((offset % bits_per_word) == 0U) {
        const auto word = offset / bits_per_word;
        return (((aval_words()[word] ^ source.aval) & mask) == 0U)
            && (((bval_words()[word] ^ source.bval) & mask) == 0U);
    }
    return (((read_word_range(aval_words(), offset, source.width)
                ^ source.aval)
               & mask)
            == 0U)
        && (((read_word_range(bval_words(), offset, source.width)
                 ^ source.bval)
                & mask)
            == 0U);
}

void PackedLogic4::insert_bits(
    const PackedLogic4& source,
    const std::size_t offset)
{
    if (source.width_ == 0U || offset > width_
        || source.width_ > width_ - offset) {
        throw std::invalid_argument(
            "insert range is outside its target value");
    }
    if (this == &source) {
        const auto stable_source = source;
        insert_bits(stable_source, offset);
        return;
    }
    if (!logic9_ && !source.logic9_) {
        copy_bit_range(
            mutable_aval_words(), source.aval_words(), offset, source.width_);
        copy_bit_range(
            mutable_bval_words(), source.bval_words(), offset, source.width_);
        mask_unused_bits();
        return;
    }

    promote_to_logic9();
    if (source.logic9_) {
        for (std::size_t plane = 0; plane < 4U; ++plane) {
            copy_bit_range(mutable_logic9_plane(plane),
                source.logic9_plane(plane), offset, source.width_);
        }
    } else {
        const auto promoted_source = source.promoted_to_logic9();
        for (std::size_t plane = 0; plane < 4U; ++plane) {
            copy_bit_range(mutable_logic9_plane(plane),
                promoted_source.logic9_plane(plane), offset,
                source.width_);
        }
    }
    mask_unused_bits();
}

PackedLogic4 PackedLogic4::extract_bits(
    const std::size_t offset,
    const std::size_t width) const
{
    if (width == 0U || offset > width_ || width > width_ - offset) {
        throw std::invalid_argument(
            "extract range is outside its source value");
    }
    PackedLogic4 result(width, Logic4::zero);
    if (!logic9_) {
        extract_bit_range(aval_words(), offset, result.mutable_aval_words());
        extract_bit_range(bval_words(), offset, result.mutable_bval_words());
    } else {
        result.promote_to_logic9();
        for (std::size_t plane = 0; plane < 4U; ++plane) {
            extract_bit_range(
                logic9_plane(plane), offset, result.mutable_logic9_plane(plane));
        }
    }
    result.mask_unused_bits();
    return result;
}

void PackedLogic4::fill(Logic4 value)
{
    if (logic9_) {
        const auto encoded = static_cast<std::uint8_t>(to_logic9(value));
        for (std::size_t plane = 0; plane < 4; ++plane) {
            auto words = mutable_logic9_plane(plane);
            std::fill(
                words.begin(),
                words.end(),
                ((encoded >> plane) & 1U) != 0
                    ? ~std::uint64_t { 0 }
                    : std::uint64_t { 0 });
        }
        mask_unused_bits();
        return;
    }
    const bool aval = value == Logic4::one || value == Logic4::x;
    const bool bval = value == Logic4::x || value == Logic4::z;
    const auto aval_words = mutable_aval_words();
    const auto bval_words = mutable_bval_words();
    std::fill(aval_words.begin(), aval_words.end(),
        aval ? ~std::uint64_t { 0 } : std::uint64_t { 0 });
    std::fill(bval_words.begin(), bval_words.end(),
        bval ? ~std::uint64_t { 0 } : std::uint64_t { 0 });
    mask_unused_bits();
}

void PackedLogic4::fill(const Logic9 value)
{
    if (!logic9_) {
        promote_to_logic9();
    }
    const auto encoded = static_cast<std::uint8_t>(value);
    for (std::size_t plane = 0; plane < 4; ++plane) {
        auto words = mutable_logic9_plane(plane);
        std::fill(
            words.begin(),
            words.end(),
            ((encoded >> plane) & 1U) != 0
                ? ~std::uint64_t { 0 }
                : std::uint64_t { 0 });
    }
    mask_unused_bits();
}

Logic9Word PackedLogic4::logic9_low_word() const
{
    if (width_ == 0 || width_ > bits_per_word) {
        throw std::invalid_argument(
            "nine-state word width must be between 1 and 64");
    }
    Logic9Word result { width_ };
    if (logic9_) {
        result.planes = {
            inline_aval_,
            inline_bval_,
            inline_logic9_plane2_,
            inline_logic9_plane3_
        };
        return result;
    }
    for (std::size_t index = 0; index < width_; ++index) {
        const auto encoded = static_cast<std::uint8_t>(to_logic9(get(index)));
        for (std::size_t plane = 0; plane < 4; ++plane) {
            if (((encoded >> plane) & 1U) != 0) {
                result.planes[plane] |= std::uint64_t { 1 } << index;
            }
        }
    }
    return result;
}

PackedLogic4 PackedLogic4::promoted_to_logic9() const
{
    auto result = *this;
    result.promote_to_logic9();
    return result;
}

std::string PackedLogic4::to_msb_string() const
{
    std::string result(width_, 'X');
    const auto aval = aval_words();
    const auto bval = bval_words();
    if (!logic9_) {
        for (std::size_t index = 0; index < width_; ++index) {
            const auto mask = std::uint64_t{1} << (index % bits_per_word);
            const auto word = index / bits_per_word;
            result[width_ - index - 1] = (bval[word] & mask) != 0U
                ? ((aval[word] & mask) != 0U ? 'X' : 'Z')
                : ((aval[word] & mask) != 0U ? '1' : '0');
        }
        return result;
    }

    constexpr std::array digits{'U', 'X', '0', '1', 'Z', 'W', 'L', 'H', '-'};
    const auto plane2 = logic9_plane(2);
    const auto plane3 = logic9_plane(3);
    for (std::size_t index = 0; index < width_; ++index) {
        const auto mask = std::uint64_t{1} << (index % bits_per_word);
        const auto word = index / bits_per_word;
        const auto encoded = static_cast<std::size_t>(
            ((aval[word] & mask) != 0U ? 1U : 0U)
            | ((bval[word] & mask) != 0U ? 2U : 0U)
            | ((plane2[word] & mask) != 0U ? 4U : 0U)
            | ((plane3[word] & mask) != 0U ? 8U : 0U));
        result[width_ - index - 1] = digits[encoded];
    }
    return result;
}

void PackedLogic4::promote_to_logic9()
{
    if (logic9_) {
        return;
    }
    std::vector<Logic4> old_values;
    old_values.reserve(width_);
    for (std::size_t index = 0; index < width_; ++index) {
        old_values.push_back(get(index));
    }
    if (width_ > bits_per_word) {
        ensure_unique_wide();
        wide_->logic9_plane2.assign(word_count(width_), 0);
        wide_->logic9_plane3.assign(word_count(width_), 0);
    }
    logic9_ = true;
    for (std::size_t index = 0; index < width_; ++index) {
        set_logic9(index, to_logic9(old_values[index]));
    }
}

std::span<const std::uint64_t>
PackedLogic4::logic9_plane(const std::size_t index) const noexcept
{
    if (width_ == 0 || index >= 4) {
        return { };
    }
    if (index == 0) {
        return aval_words();
    }
    if (index == 1) {
        return bval_words();
    }
    if (width_ <= bits_per_word) {
        return index == 2
            ? std::span<const std::uint64_t> {
                  &inline_logic9_plane2_, 1
              }
            : std::span<const std::uint64_t> { &inline_logic9_plane3_, 1 };
    }
    return index == 2
        ? std::span<const std::uint64_t> { wide_->logic9_plane2 }
        : std::span<const std::uint64_t> { wide_->logic9_plane3 };
}

std::span<std::uint64_t>
PackedLogic4::mutable_logic9_plane(
    const std::size_t index)
{
    if (width_ == 0 || index >= 4) {
        return { };
    }
    if (index == 0) {
        return mutable_aval_words();
    }
    if (index == 1) {
        return mutable_bval_words();
    }
    if (width_ <= bits_per_word) {
        return index == 2
            ? std::span<std::uint64_t> {
                  &inline_logic9_plane2_, 1
              }
            : std::span<std::uint64_t> { &inline_logic9_plane3_, 1 };
    }
    ensure_unique_wide();
    return index == 2
        ? std::span<std::uint64_t> { wide_->logic9_plane2 }
        : std::span<std::uint64_t> { wide_->logic9_plane3 };
}

void PackedLogic4::mask_unused_bits()
{
    mask_last(mutable_aval_words(), width_);
    mask_last(mutable_bval_words(), width_);
    if (logic9_) {
        mask_last(mutable_logic9_plane(2), width_);
        mask_last(mutable_logic9_plane(3), width_);
    }
}

PackedLogic9::PackedLogic9(std::size_t width, Logic9 initial)
    : width_(width)
{
    const auto count = word_count(width);
    for (auto& plane : planes_) {
        plane.resize(count);
    }
    fill(initial);
}

PackedLogic9 PackedLogic9::from_msb_string(std::string_view value)
{
    PackedLogic9 result(value.size(), Logic9::u);
    for (std::size_t offset = 0; offset < value.size(); ++offset) {
        const auto parsed = parse_logic9(value[value.size() - offset - 1]);
        if (!parsed) {
            throw std::invalid_argument("nine-state vector contains an invalid digit");
        }
        result.set(offset, *parsed);
    }
    return result;
}

Logic9 PackedLogic9::get(std::size_t index) const
{
    check_index(index, width_);
    std::uint8_t encoded = 0;
    for (std::size_t plane = 0; plane < planes_.size(); ++plane) {
        const auto bit = static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(read_bit(planes_[plane], index)) << plane);
        encoded = static_cast<std::uint8_t>(encoded | bit);
    }
    return encoded <= static_cast<std::uint8_t>(Logic9::dont_care)
        ? static_cast<Logic9>(encoded)
        : Logic9::x;
}

void PackedLogic9::set(std::size_t index, Logic9 value)
{
    check_index(index, width_);
    const auto encoded = static_cast<std::uint8_t>(value);
    for (std::size_t plane = 0; plane < planes_.size(); ++plane) {
        write_bit(planes_[plane], index, ((encoded >> plane) & 1U) != 0);
    }
}

void PackedLogic9::fill(Logic9 value) noexcept
{
    const auto encoded = static_cast<std::uint8_t>(value);
    for (std::size_t plane = 0; plane < planes_.size(); ++plane) {
        std::fill(planes_[plane].begin(), planes_[plane].end(),
            ((encoded >> plane) & 1U) != 0 ? ~std::uint64_t { 0 }
                                           : std::uint64_t { 0 });
    }
    mask_unused_bits();
}

std::span<const std::uint64_t>
PackedLogic9::plane(std::size_t index) const
{
    if (index >= planes_.size()) {
        throw std::out_of_range("nine-state vector plane index is out of range");
    }
    return planes_[index];
}

std::string PackedLogic9::to_msb_string() const
{
    std::string result(width_, 'U');
    for (std::size_t index = 0; index < width_; ++index) {
        result[width_ - index - 1] = to_char(get(index));
    }
    return result;
}

void PackedLogic9::mask_unused_bits() noexcept
{
    for (auto& plane : planes_) {
        mask_last(plane, width_);
    }
}

PackedLogic4 collapse_to_logic4(const PackedLogic9& value)
{
    PackedLogic4 result(value.width(), Logic4::zero);
    for (std::size_t index = 0; index < value.width(); ++index) {
        result.set(index, to_logic4(value.get(index)));
    }
    return result;
}

PackedLogic4 collapse_to_logic4(const PackedLogic4& value)
{
    if (!value.is_logic9()) {
        return value;
    }
    PackedLogic4 result(value.width(), Logic4::zero);
    for (std::size_t index = 0; index < value.width(); ++index) {
        result.set(index, to_logic4(value.get_logic9(index)));
    }
    return result;
}

PackedLogic9 expand_to_logic9(const PackedLogic4& value)
{
    PackedLogic9 result(value.width(), Logic9::zero);
    for (std::size_t index = 0; index < value.width(); ++index) {
        result.set(index, value.get_logic9(index));
    }
    return result;
}

PackedLogic4 resolve(std::span<const PackedLogic4> drivers)
{
    if (drivers.empty()) {
        return PackedLogic4 { };
    }
    if (drivers.size() == 1) {
        return drivers.front();
    }
    const auto width = drivers.front().width();
    const auto logic9 = std::ranges::any_of(
        drivers,
        [](const PackedLogic4& driver) {
            return driver.is_logic9();
        });
    if (!logic9) {
        if (width == 0U) {
            return PackedLogic4 { };
        }
        const auto words = (width + bits_per_word - 1U) / bits_per_word;
        std::vector<std::uint64_t> zero_seen(words);
        std::vector<std::uint64_t> one_seen(words);
        std::vector<std::uint64_t> unknown_seen(words);
        for (const auto& driver : drivers) {
            if (driver.width() != width) {
                throw std::invalid_argument(
                    "cannot resolve drivers of different widths");
            }
            const auto aval = driver.aval_words();
            const auto bval = driver.bval_words();
            for (std::size_t word = 0; word < words; ++word) {
                const auto known = ~bval[word];
                zero_seen[word] |= ~aval[word] & known;
                one_seen[word] |= aval[word] & known;
                unknown_seen[word] |= aval[word] & bval[word];
            }
        }
        std::vector<std::uint64_t> resolved_aval(words);
        std::vector<std::uint64_t> resolved_bval(words);
        for (std::size_t word = 0; word < words; ++word) {
            const auto driven
                = zero_seen[word] | one_seen[word] | unknown_seen[word];
            resolved_aval[word] = one_seen[word] | unknown_seen[word];
            resolved_bval[word] = unknown_seen[word]
                | (zero_seen[word] & one_seen[word]) | ~driven;
        }
        return PackedLogic4::from_word_planes(
            width, resolved_aval, resolved_bval);
    }
    if (width == 0U) {
        return PackedLogic4 { };
    }
    if (width <= bits_per_word) {
        std::array<std::uint64_t, 7U> seen { };
        for (const auto& driver : drivers) {
            if (driver.width() != width) {
                throw std::invalid_argument(
                    "cannot resolve drivers of different widths");
            }
            const auto p0 = driver.aval_words().front();
            const auto p1 = driver.bval_words().front();
            if (!driver.is_logic9()) {
                const auto known = ~p1;
                seen[2] |= ~p0 & known;
                seen[3] |= p0 & known;
                seen[1] |= p0 & p1;
                continue;
            }
            const auto p2 = driver.logic9_plane_words(2U).front();
            const auto p3 = driver.logic9_plane_words(3U).front();
            const auto np0 = ~p0;
            const auto np1 = ~p1;
            const auto np2 = ~p2;
            const auto np3 = ~p3;
            const auto upper_zero = np2 & np3;
            const auto upper_four = p2 & np3;
            const auto state_u = np0 & np1 & upper_zero;
            const auto state_x = p0 & np1 & upper_zero;
            const auto state_zero = np0 & p1 & upper_zero;
            const auto state_one = p0 & p1 & upper_zero;
            const auto state_z = np0 & np1 & upper_four;
            const auto state_w = p0 & np1 & upper_four;
            const auto state_l = np0 & p1 & upper_four;
            const auto state_h = p0 & p1 & upper_four;
            const auto state_dont_care = np0 & np1 & np2 & p3;
            const auto valid = state_u | state_x | state_zero | state_one
                | state_z | state_w | state_l | state_h
                | state_dont_care;
            seen[0] |= state_u;
            seen[1] |= state_x | state_dont_care | ~valid;
            seen[2] |= state_zero;
            seen[3] |= state_one;
            seen[4] |= state_w;
            seen[5] |= state_l;
            seen[6] |= state_h;
        }
        const auto strong_conflict = seen[2] & seen[3];
        const auto state_u = seen[0];
        const auto state_x = ~state_u & (seen[1] | strong_conflict);
        const auto strong_domain = ~(state_u | state_x);
        const auto state_zero = strong_domain & seen[2] & ~seen[3];
        const auto state_one = strong_domain & seen[3] & ~seen[2];
        const auto weak_domain = strong_domain & ~seen[2] & ~seen[3];
        const auto state_w = weak_domain & (seen[4] | (seen[5] & seen[6]));
        const auto state_l = weak_domain & ~state_w & seen[5] & ~seen[6];
        const auto state_h = weak_domain & ~state_w & seen[6] & ~seen[5];
        const auto state_z = weak_domain & ~(state_w | state_l | state_h);
        return PackedLogic4::from_logic9_word({
            width,
            { state_x | state_one | state_w | state_h,
                state_zero | state_one | state_l | state_h,
                state_z | state_w | state_l | state_h,
                0U }
        });
    }
    const auto words = (width + bits_per_word - 1U) / bits_per_word;
    std::vector<std::uint64_t> u_seen(words);
    std::vector<std::uint64_t> x_seen(words);
    std::vector<std::uint64_t> zero_seen(words);
    std::vector<std::uint64_t> one_seen(words);
    std::vector<std::uint64_t> w_seen(words);
    std::vector<std::uint64_t> l_seen(words);
    std::vector<std::uint64_t> h_seen(words);
    for (const auto& driver : drivers) {
        if (driver.width() != width) {
            throw std::invalid_argument("cannot resolve drivers of different widths");
        }
        const auto plane0 = driver.aval_words();
        const auto plane1 = driver.bval_words();
        const auto plane2 = driver.logic9_plane_words(2U);
        const auto plane3 = driver.logic9_plane_words(3U);
        for (std::size_t word = 0; word < words; ++word) {
            const auto p0 = plane0[word];
            const auto p1 = plane1[word];
            if (!driver.is_logic9()) {
                const auto known = ~p1;
                zero_seen[word] |= ~p0 & known;
                one_seen[word] |= p0 & known;
                x_seen[word] |= p0 & p1;
                continue;
            }
            const auto p2 = plane2[word];
            const auto p3 = plane3[word];
            const auto np0 = ~p0;
            const auto np1 = ~p1;
            const auto np2 = ~p2;
            const auto np3 = ~p3;
            const auto upper_zero = np2 & np3;
            const auto upper_four = p2 & np3;
            const auto state_u = np0 & np1 & upper_zero;
            const auto state_x = p0 & np1 & upper_zero;
            const auto state_zero = np0 & p1 & upper_zero;
            const auto state_one = p0 & p1 & upper_zero;
            const auto state_z = np0 & np1 & upper_four;
            const auto state_w = p0 & np1 & upper_four;
            const auto state_l = np0 & p1 & upper_four;
            const auto state_h = p0 & p1 & upper_four;
            const auto state_dont_care = np0 & np1 & np2 & p3;
            const auto valid = state_u | state_x | state_zero | state_one
                | state_z | state_w | state_l | state_h
                | state_dont_care;
            u_seen[word] |= state_u;
            x_seen[word] |= state_x | state_dont_care | ~valid;
            zero_seen[word] |= state_zero;
            one_seen[word] |= state_one;
            w_seen[word] |= state_w;
            l_seen[word] |= state_l;
            h_seen[word] |= state_h;
        }
    }
    std::vector<std::uint64_t> resolved0(words);
    std::vector<std::uint64_t> resolved1(words);
    std::vector<std::uint64_t> resolved2(words);
    std::vector<std::uint64_t> resolved3(words);
    for (std::size_t word = 0; word < words; ++word) {
        const auto strong_conflict = zero_seen[word] & one_seen[word];
        const auto state_u = u_seen[word];
        const auto state_x = ~state_u & (x_seen[word] | strong_conflict);
        const auto strong_domain = ~(state_u | state_x);
        const auto state_zero
            = strong_domain & zero_seen[word] & ~one_seen[word];
        const auto state_one
            = strong_domain & one_seen[word] & ~zero_seen[word];
        const auto weak_domain
            = strong_domain & ~zero_seen[word] & ~one_seen[word];
        const auto state_w
            = weak_domain & (w_seen[word] | (l_seen[word] & h_seen[word]));
        const auto state_l
            = weak_domain & ~state_w & l_seen[word] & ~h_seen[word];
        const auto state_h
            = weak_domain & ~state_w & h_seen[word] & ~l_seen[word];
        const auto state_z
            = weak_domain & ~(state_w | state_l | state_h);
        resolved0[word] = state_x | state_one | state_w | state_h;
        resolved1[word] = state_zero | state_one | state_l | state_h;
        resolved2[word] = state_z | state_w | state_l | state_h;
        resolved3[word] = 0U;
    }
    return PackedLogic4::from_logic9_word_planes(
        width, resolved0, resolved1, resolved2, resolved3);
}

PackedLogic9 resolve(std::span<const PackedLogic9> drivers)
{
    if (drivers.empty()) {
        return PackedLogic9 { };
    }
    if (drivers.size() == 1) {
        return drivers.front();
    }
    const auto width = drivers.front().width();
    PackedLogic9 result(width, Logic9::z);
    for (const auto& driver : drivers) {
        if (driver.width() != width) {
            throw std::invalid_argument("cannot resolve drivers of different widths");
        }
        for (std::size_t index = 0; index < width; ++index) {
            result.set(index, resolve(result.get(index), driver.get(index)));
        }
    }
    return result;
}

} // namespace fsim::runtime
