// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/packed_value.hpp"

#include <algorithm>
#include <cassert>
#include <bit>
#include <exception>
#include <functional>
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

    [[nodiscard]] std::size_t checked_packed_width(
        const std::size_t width)
    {
        constexpr auto maximum_width
            = std::numeric_limits<std::size_t>::max() / 4U;
        if (width > maximum_width) {
            throw std::length_error("packed value width is too large");
        }
        return width;
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

    [[nodiscard]] constexpr Logic9 canonical_logic9_value(
        const Logic9 value) noexcept
    {
        return static_cast<std::uint8_t>(value)
                <= static_cast<std::uint8_t>(Logic9::dont_care)
            ? value
            : Logic9::x;
    }

    void mask_last(std::span<std::uint64_t> words, std::size_t width) noexcept
    {
        if (!words.empty()) {
            words.back() &= final_word_mask(width);
        }
    }

    [[nodiscard]] bool word_spans_overlap(
        const std::span<const std::uint64_t> left,
        const std::span<const std::uint64_t> right) noexcept
    {
        if (left.empty() || right.empty()) {
            return false;
        }
        const std::less<const std::uint64_t*> less;
        const auto* const left_end = left.data() + left.size();
        const auto* const right_end = right.data() + right.size();
        return less(left.data(), right_end)
            && less(right.data(), left_end);
    }

    [[nodiscard]] bool exact_word_span(
        const std::span<const std::uint64_t> input,
        const std::span<const std::uint64_t> destination) noexcept
    {
        return input.data() == destination.data()
            && input.size() == destination.size();
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

Logic4ResolutionAccumulator::Logic4ResolutionAccumulator(
    const std::size_t width)
    : width_ { width }
    , width_mask_ { final_word_mask(width) }
{
    if (width == 0U || width > bits_per_word) {
        throw std::invalid_argument {
            "Logic4 word resolution width must be between 1 and 64"
        };
    }
}

void Logic4ResolutionAccumulator::add(const Logic4Word driver)
{
    if (driver.width != width_) {
        throw std::invalid_argument {
            "Logic4 word resolution driver width mismatch"
        };
    }
    const auto aval = driver.aval & width_mask_;
    const auto bval = driver.bval & width_mask_;
    const auto known = ~bval & width_mask_;
    zero_seen_ |= ~aval & known;
    one_seen_ |= aval & known;
    unknown_seen_ |= aval & bval;
}

Logic4Word Logic4ResolutionAccumulator::result() const noexcept
{
    const auto driven = zero_seen_ | one_seen_ | unknown_seen_;
    return {
        width_,
        (one_seen_ | unknown_seen_) & width_mask_,
        (unknown_seen_ | (zero_seen_ & one_seen_)
            | (~driven & width_mask_))
            & width_mask_
    };
}

Logic4Word apply_force_word(
    Logic4Word driven,
    const Logic4Word& forced,
    const std::uint64_t mask)
{
    if (driven.width == 0U || driven.width > bits_per_word
        || forced.width != driven.width) {
        throw std::invalid_argument {
            "Logic4 force words must have the same width between 1 and 64"
        };
    }
    const auto width_mask = final_word_mask(driven.width);
    const auto force_mask = mask & width_mask;
    driven.aval = ((driven.aval & ~force_mask)
        | (forced.aval & force_mask))
        & width_mask;
    driven.bval = ((driven.bval & ~force_mask)
        | (forced.bval & force_mask))
        & width_mask;
    return driven;
}

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

void PackedLogic4PlaneBlock::acquire_read_pin() noexcept
{
    auto state = reader_state_.load(std::memory_order_acquire);
    for (;;) {
        if ((state & writer_bit) != 0U) {
            state = reader_state_.load(std::memory_order_acquire);
            continue;
        }
        if (state == writer_bit - 1U) {
            std::terminate();
        }
        if (reader_state_.compare_exchange_weak(state, state + 1U,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            return;
        }
    }
}

void PackedLogic4PlaneBlock::release_read_pin() noexcept
{
    [[maybe_unused]] const auto previous
        = reader_state_.fetch_sub(1U, std::memory_order_acq_rel);
    assert(previous != 0U);
    assert((previous & writer_bit) == 0U);
}

bool PackedLogic4PlaneBlock::has_read_pins() const noexcept
{
    return reader_state_.load(std::memory_order_acquire) != 0U;
}

bool PackedLogic4PlaneBlock::write_locked() const noexcept
{
    return (reader_state_.load(std::memory_order_acquire) & writer_bit) != 0U;
}

bool PackedLogic4PlaneBlock::try_begin_write() noexcept
{
    auto expected = std::size_t { 0 };
    return reader_state_.compare_exchange_strong(expected, writer_bit,
        std::memory_order_acq_rel, std::memory_order_acquire);
}

void PackedLogic4PlaneBlock::end_write() noexcept
{
    assert(reader_state_.load(std::memory_order_relaxed) == writer_bit);
    reader_state_.store(0U, std::memory_order_release);
}

std::shared_ptr<PackedLogic4PlaneBlock> PackedLogic4PlaneBlock::clone(
    const PackedLogic4PlaneBlock& source)
{
    auto result = std::make_shared<PackedLogic4PlaneBlock>();
    for (std::size_t plane = 0U; plane < result->planes.size(); ++plane) {
        result->planes[plane] = source.planes[plane];
    }
    return result;
}

std::shared_ptr<PackedLogic4PlaneBlock>
PackedLogic4PlaneCell::acquire_read_block() const noexcept
{
    for (;;) {
        auto block = current.load(std::memory_order_acquire);
        if (!block) {
            return { };
        }
        block->acquire_read_pin();
        if (current.load(std::memory_order_acquire) == block) {
            return block;
        }
        block->release_read_pin();
    }
}

std::span<std::uint64_t> PackedLogic4PlaneBacking::plane_words(
    const std::size_t plane) const noexcept
{
    if (plane >= 4U) {
        return { };
    }
    if (!cell) {
        return unversioned_planes[plane];
    }
    const auto block = cell->current.load(std::memory_order_acquire);
    if (!block) {
        return { };
    }
    const auto first = plane < 2U
        ? first_value_word : first_logic9_word;
    const auto words = width / bits_per_word
        + (width % bits_per_word == 0U ? 0U : 1U);
    auto& storage = block->planes[plane];
    if (first > storage.size() || words > storage.size() - first) {
        return { };
    }
    return storage.span().subspan(first, words);
}

std::shared_ptr<PackedLogic4PlaneBlock>
PackedLogic4PlaneBacking::acquire_read_block() const noexcept
{
    return cell ? cell->acquire_read_block()
                : std::shared_ptr<PackedLogic4PlaneBlock> { };
}

PackedLogic4PlaneReadLease::PackedLogic4PlaneReadLease(
    std::shared_ptr<PackedLogic4PlaneBlock> block,
    const std::size_t width,
    const bool logic9,
    const std::size_t first_value_word,
    const std::size_t first_logic9_word) noexcept
    : block_ { std::move(block) }
    , width_ { width }
    , logic9_ { logic9 }
    , first_value_word_ { first_value_word }
    , first_logic9_word_ { first_logic9_word }
{
}

PackedLogic4PlaneReadLease::PackedLogic4PlaneReadLease(
    const PackedLogic4PlaneReadLease& other) noexcept
    : block_ { other.block_ }
    , width_ { other.width_ }
    , logic9_ { other.logic9_ }
    , first_value_word_ { other.first_value_word_ }
    , first_logic9_word_ { other.first_logic9_word_ }
{
    if (block_) {
        block_->acquire_read_pin();
    }
}

PackedLogic4PlaneReadLease::PackedLogic4PlaneReadLease(
    PackedLogic4PlaneReadLease&& other) noexcept
    : block_ { std::move(other.block_) }
    , width_ { other.width_ }
    , logic9_ { other.logic9_ }
    , first_value_word_ { other.first_value_word_ }
    , first_logic9_word_ { other.first_logic9_word_ }
{
}

PackedLogic4PlaneReadLease& PackedLogic4PlaneReadLease::operator=(
    const PackedLogic4PlaneReadLease& other) noexcept
{
    if (this == &other) {
        return *this;
    }
    auto replacement = other.block_;
    if (replacement) {
        replacement->acquire_read_pin();
    }
    reset();
    block_ = std::move(replacement);
    width_ = other.width_;
    logic9_ = other.logic9_;
    first_value_word_ = other.first_value_word_;
    first_logic9_word_ = other.first_logic9_word_;
    return *this;
}

PackedLogic4PlaneReadLease& PackedLogic4PlaneReadLease::operator=(
    PackedLogic4PlaneReadLease&& other) noexcept
{
    if (this == &other) {
        return *this;
    }
    reset();
    block_ = std::move(other.block_);
    width_ = other.width_;
    logic9_ = other.logic9_;
    first_value_word_ = other.first_value_word_;
    first_logic9_word_ = other.first_logic9_word_;
    return *this;
}

PackedLogic4PlaneReadLease::~PackedLogic4PlaneReadLease()
{
    reset();
}

void PackedLogic4PlaneReadLease::reset() noexcept
{
    if (block_) {
        block_->release_read_pin();
        block_.reset();
    }
}

PackedLogic4PlaneStorage::PackedLogic4PlaneStorage(
    const PackedLogic4PlaneStorage& other)
{
    if (!other.empty()) {
        owned_.assign(other.begin(), other.end());
    }
}

PackedLogic4PlaneStorage::PackedLogic4PlaneStorage(
    PackedLogic4PlaneStorage&& other)
{
    if (other.borrowed_storage_) {
        if (!other.borrowed_.empty()) {
            owned_.assign(other.borrowed_.begin(), other.borrowed_.end());
        }
    } else {
        owned_ = std::move(other.owned_);
    }
}

PackedLogic4PlaneStorage& PackedLogic4PlaneStorage::operator=(
    const PackedLogic4PlaneStorage& other)
{
    if (this == &other) {
        return *this;
    }
    std::vector<std::uint64_t> replacement;
    if (!other.empty()) {
        replacement.assign(other.begin(), other.end());
    }
    owned_ = std::move(replacement);
    borrowed_ = { };
    borrowed_storage_ = false;
    return *this;
}

PackedLogic4PlaneStorage& PackedLogic4PlaneStorage::operator=(
    PackedLogic4PlaneStorage&& other)
{
    if (this == &other) {
        return *this;
    }
    if (other.borrowed_storage_) {
        std::vector<std::uint64_t> replacement;
        if (!other.borrowed_.empty()) {
            replacement.assign(
                other.borrowed_.begin(), other.borrowed_.end());
        }
        owned_ = std::move(replacement);
        borrowed_ = { };
        borrowed_storage_ = false;
    } else {
        owned_ = std::move(other.owned_);
        borrowed_ = { };
        borrowed_storage_ = false;
    }
    return *this;
}

std::size_t PackedLogic4PlaneStorage::size() const noexcept
{
    return borrowed_storage_ ? borrowed_.size() : owned_.size();
}

std::uint64_t* PackedLogic4PlaneStorage::data() noexcept
{
    return borrowed_storage_ ? borrowed_.data() : owned_.data();
}

const std::uint64_t* PackedLogic4PlaneStorage::data() const noexcept
{
    return borrowed_storage_ ? borrowed_.data() : owned_.data();
}

void PackedLogic4PlaneStorage::resize(const std::size_t size)
{
    if (!borrowed_storage_) {
        owned_.resize(size);
        return;
    }
    if (size == borrowed_.size()) {
        return;
    }
    std::vector<std::uint64_t> replacement;
    if (!borrowed_.empty()) {
        replacement.assign(borrowed_.begin(), borrowed_.end());
    }
    replacement.resize(size);
    owned_ = std::move(replacement);
    borrowed_ = { };
    borrowed_storage_ = false;
}

void PackedLogic4PlaneStorage::bind(
    const std::span<std::uint64_t> words) noexcept
{
    assert(words.size() == size());
    std::vector<std::uint64_t> { }.swap(owned_);
    borrowed_ = words;
    borrowed_storage_ = true;
}

std::span<const std::uint64_t>
PackedLogic4PlaneReadLease::plane_words(const std::size_t plane) const noexcept
{
    if (!block_ || plane >= 4U || (!logic9_ && plane >= 2U)) {
        return { };
    }
    const auto first = plane < 2U
        ? first_value_word_ : first_logic9_word_;
    const auto words = width_ / bits_per_word
        + (width_ % bits_per_word == 0U ? 0U : 1U);
    const auto& storage = block_->planes[plane];
    if (first > storage.size() || words > storage.size() - first) {
        return { };
    }
    return storage.span().subspan(first, words);
}

PackedLogic4::PackedLogic4(std::size_t width, Logic4 initial)
    : width_and_logic9_(checked_packed_width(width))
{
    if (uses_inline_storage()) {
        std::construct_at(&storage_.inline_value);
    } else {
        std::construct_at(&storage_.indirect);
        std::construct_at(&extra_storage().wide,
            std::make_shared<WideStorage>(word_count(this->width())));
    }
    fill(initial);
}

void PackedLogic4::initialize_storage_from(const PackedLogic4& other,
    std::shared_ptr<WideStorage> wide) noexcept
{
    if (other.has_live_plane_backing()) {
        if (other.plane_backing()->cell) {
            capture_plane_snapshot(*other.plane_backing());
        } else {
            // Unversioned A4 backing is restricted to narrow values.
            assert(width() <= bits_per_word);
            std::construct_at(&storage_.inline_value);
            const auto aval = other.aval_words();
            const auto bval = other.bval_words();
            inline_aval() = aval.empty() ? 0U : aval.front();
            inline_bval() = bval.empty() ? 0U : bval.front();
            if (is_logic9()) {
                inline_logic9_plane2() = other.logic9_plane_words(2U).front();
                inline_logic9_plane3() = other.logic9_plane_words(3U).front();
            }
        }
    } else if (other.has_plane_snapshot()) {
        std::construct_at(&storage_.indirect);
        std::construct_at(&extra_storage().plane_snapshot,
            PlaneSnapshotStorage { other.extra_storage().plane_snapshot.block });
        set_snapshot_offsets(other.snapshot_first_value_word(),
            other.snapshot_first_logic9_word());
        extra_storage().plane_snapshot.block->acquire_read_pin();
        width_and_logic9_ |= plane_backing_mask;
        plane_snapshot_ = true;
    } else if (uses_inline_storage()) {
        std::construct_at(&storage_.inline_value, other.storage_.inline_value);
    } else {
        std::construct_at(&storage_.indirect);
        std::construct_at(&extra_storage().wide, std::move(wide));
    }
}

void PackedLogic4::copy_construct_slow(const PackedLogic4& other)
{
    initialize_storage_from(other,
        other.has_external_planes() || other.uses_inline_storage()
            ? std::shared_ptr<WideStorage> { } : other.wide_storage());
}

void PackedLogic4::move_construct_slow(PackedLogic4&& other) noexcept
{
    initialize_storage_from(other,
        other.has_external_planes() || other.uses_inline_storage()
            ? std::shared_ptr<WideStorage> { } : std::move(other.wide_storage()));
}

PackedLogic4& PackedLogic4::copy_assign_slow(const PackedLogic4& other)
{
    if (this == &other) {
        return *this;
    }
    if (has_live_plane_backing()) {
        if (width() != other.width() || is_logic9() != other.is_logic9()) {
            throw std::invalid_argument {
                "assigned value does not match an authoritative packed slot"
            };
        }
        const auto plane_count = is_logic9() ? 4U : 2U;
        const auto* const backing = plane_backing();
        if (backing->cell) {
            for (;;) {
                auto block = backing->cell->current.load(
                    std::memory_order_acquire);
                if (!block) {
                    throw std::logic_error {
                        "authoritative packed slot lost its plane block"
                    };
                }
                if (block->try_begin_write()) {
                    if (backing->cell->current.load(
                            std::memory_order_acquire) != block) {
                        block->end_write();
                        continue;
                    }
                    for (std::size_t plane = 0U;
                        plane < plane_count; ++plane) {
                        const auto source = plane == 0U
                            ? other.aval_words()
                            : plane == 1U ? other.bval_words()
                                          : other.logic9_plane_words(plane);
                        std::ranges::copy(
                            source, backing->plane_words(plane).begin());
                    }
                    block->end_write();
                    return *this;
                }
                if (block->write_locked()) {
                    continue;
                }
                auto replacement = PackedLogic4PlaneBlock::clone(*block);
                for (std::size_t plane = 0U;
                    plane < plane_count; ++plane) {
                    const auto source = plane == 0U
                        ? other.aval_words()
                        : plane == 1U ? other.bval_words()
                                      : other.logic9_plane_words(plane);
                    const auto first = plane < 2U
                        ? backing->first_value_word
                        : backing->first_logic9_word;
                    std::ranges::copy(source,
                        replacement->planes[plane].begin()
                            + static_cast<std::ptrdiff_t>(first));
                }
                if (backing->cell->current.load(
                        std::memory_order_acquire) != block) {
                    continue;
                }
                backing->cell->current.store(
                    std::move(replacement), std::memory_order_release);
                return *this;
            }
        }
        for (std::size_t plane = 0U; plane < plane_count; ++plane) {
            const auto source = plane == 0U ? other.aval_words()
                : plane == 1U ? other.bval_words()
                              : other.logic9_plane_words(plane);
            std::ranges::copy(
                source, prepare_plane_storage_for_write(plane).begin());
        }
        return *this;
    }
    PackedLogic4 replacement { other };
    return *this = std::move(replacement);
}

PackedLogic4& PackedLogic4::move_assign_slow(PackedLogic4&& other) noexcept
{
    if (this == &other) {
        return *this;
    }
    if (has_live_plane_backing()) {
        if (width() != other.width() || is_logic9() != other.is_logic9()) {
            std::terminate();
        }
        const auto* const backing = plane_backing();
        if (backing->cell && other.has_plane_snapshot()
            && other.extra_storage().plane_snapshot.block
                == backing->cell->current.load(std::memory_order_acquire)
            && other.snapshot_first_value_word()
                == backing->first_value_word
            && other.snapshot_first_logic9_word()
                == backing->first_logic9_word) {
            return *this;
        }
        auto block = backing->cell
            ? backing->cell->current.load(std::memory_order_acquire)
            : std::shared_ptr<PackedLogic4PlaneBlock> { };
        if (block && !block->try_begin_write()) {
            // A4 publication preflights and installs replacement blocks
            // before entering this nonthrowing slot assignment.
            std::terminate();
        }
        const auto plane_count = is_logic9() ? 4U : 2U;
        for (std::size_t plane = 0U; plane < plane_count; ++plane) {
            const auto source = plane == 0U ? other.aval_words()
                : plane == 1U ? other.bval_words()
                              : other.logic9_plane_words(plane);
            std::ranges::copy(source,
                backing->plane_words(plane).begin());
        }
        if (block) {
            block->end_write();
        }
        return *this;
    }
    destroy_active_storage();
    width_and_logic9_ = other.width_and_logic9_ & ~plane_backing_mask;
    initialize_storage_from(other,
        other.has_external_planes() || other.uses_inline_storage()
            ? std::shared_ptr<WideStorage> { } : std::move(other.wide_storage()));
    return *this;
}

void PackedLogic4::bind_plane_backing(
    const PackedLogic4PlaneBacking& backing) noexcept
{
    assert(!has_live_plane_backing());
    assert(width() != 0U);
    assert(backing.width == width());
    assert(backing.logic9 == is_logic9());
    [[maybe_unused]] const auto words = word_count(width());
    assert(backing.plane_words(0U).size() == words
        && backing.plane_words(1U).size() == words);
    assert(is_logic9()
        ? (backing.plane_words(2U).size() == words
            && backing.plane_words(3U).size() == words)
        : (backing.plane_words(2U).empty()
            && backing.plane_words(3U).empty()));
    destroy_active_storage();
    std::construct_at(&storage_.indirect);
    std::construct_at(&extra_storage().plane_backing, &backing);
    width_and_logic9_ |= plane_backing_mask;
    plane_snapshot_ = false;
}

void PackedLogic4::unbind_plane_backing() noexcept
{
    if (!has_live_plane_backing()) {
        return;
    }
    const auto* const backing = extra_storage().plane_backing;
    const auto aval = backing->plane_words(0U);
    const auto bval = backing->plane_words(1U);
    if (width() > bits_per_word) {
        auto snapshot = PlaneSnapshotStorage {
            backing->acquire_read_block(),
        };
        assert(snapshot.block != nullptr);
        assert(aval.size() == word_count(width())
            && bval.size() == word_count(width()));
        std::destroy_at(&extra_storage().plane_backing);
        std::construct_at(&extra_storage().plane_snapshot, std::move(snapshot));
        set_snapshot_offsets(
            backing->first_value_word, backing->first_logic9_word);
        plane_snapshot_ = true;
        return;
    }
    assert(aval.size() == 1U && bval.size() == 1U);
    InlineStorage value;
    value.aval[0U] = aval.front();
    value.bval[0U] = bval.front();
    if (is_logic9()) {
        const auto plane2 = backing->plane_words(2U);
        const auto plane3 = backing->plane_words(3U);
        assert(plane2.size() == 1U && plane3.size() == 1U);
        value.aval[1U] = plane2.front();
        value.bval[1U] = plane3.front();
    }
    std::destroy_at(&extra_storage().plane_backing);
    std::destroy_at(&storage_.indirect);
    std::construct_at(&storage_.inline_value, value);
    width_and_logic9_ &= ~plane_backing_mask;
    plane_snapshot_ = false;
}

void PackedLogic4::destroy_active_storage() noexcept
{
    if (has_live_plane_backing()) {
        std::destroy_at(&extra_storage().plane_backing);
        std::destroy_at(&storage_.indirect);
    } else if (has_plane_snapshot()) {
        release_plane_snapshot();
        std::destroy_at(&extra_storage().plane_snapshot);
        std::destroy_at(&storage_.indirect);
    } else if (uses_inline_storage()) {
        std::destroy_at(&storage_.inline_value);
    } else {
        std::destroy_at(&extra_storage().wide);
        std::destroy_at(&storage_.indirect);
    }
    plane_snapshot_ = false;
}

void PackedLogic4::capture_plane_snapshot(
    const PackedLogic4PlaneBacking& backing) noexcept
{
    auto snapshot = PlaneSnapshotStorage {
        backing.acquire_read_block(),
    };
    assert(snapshot.block != nullptr);
    std::construct_at(&storage_.indirect);
    std::construct_at(&extra_storage().plane_snapshot, std::move(snapshot));
    set_snapshot_offsets(
        backing.first_value_word, backing.first_logic9_word);
    width_and_logic9_ |= plane_backing_mask;
    plane_snapshot_ = true;
}

void PackedLogic4::release_plane_snapshot() noexcept
{
    if (has_plane_snapshot() && extra_storage().plane_snapshot.block) {
        extra_storage().plane_snapshot.block->release_read_pin();
    }
}

std::span<const std::uint64_t>
PackedLogic4::snapshot_plane_words(const std::size_t plane) const noexcept
{
    if (!has_plane_snapshot() || plane >= 4U
        || (!is_logic9() && plane >= 2U)) {
        return { };
    }
    const auto first = plane < 2U
        ? snapshot_first_value_word() : snapshot_first_logic9_word();
    const auto words = word_count(width());
    const auto& source = extra_storage().plane_snapshot.block->planes[plane];
    if (first > source.size() || words > source.size() - first) {
        return { };
    }
    return source.span().subspan(first, words);
}

PackedLogic4PlaneReadLease PackedLogic4::plane_read_lease() const noexcept
{
    if (has_plane_snapshot()) {
        auto block = extra_storage().plane_snapshot.block;
        if (block) {
            block->acquire_read_pin();
        }
        return PackedLogic4PlaneReadLease { std::move(block), width(),
            is_logic9(), snapshot_first_value_word(),
            snapshot_first_logic9_word() };
    }
    if (!has_live_plane_backing()) {
        return { };
    }
    const auto* const backing = plane_backing();
    return PackedLogic4PlaneReadLease { backing->acquire_read_block(),
        width(), is_logic9(), backing->first_value_word,
        backing->first_logic9_word };
}

std::span<std::uint64_t> PackedLogic4::prepare_plane_storage_for_write(
    const std::size_t plane)
{
    if (plane >= (is_logic9() ? 4U : 2U)) {
        return { };
    }
    if (has_plane_snapshot()) {
        materialize_plane_snapshot();
    }
    if (has_live_plane_backing()) {
        const auto* const backing = plane_backing();
        if (backing->cell) {
            for (;;) {
                auto block
                    = backing->cell->current.load(std::memory_order_acquire);
                if (!block) {
                    return { };
                }
                if (block->write_locked()) {
                    continue;
                }
                if (!block->has_read_pins()) {
                    break;
                }
                auto replacement = PackedLogic4PlaneBlock::clone(*block);
                if (backing->cell->current.load(std::memory_order_acquire)
                    != block) {
                    continue;
                }
                backing->cell->current.store(
                    std::move(replacement), std::memory_order_release);
                break;
            }
            // Callers serialize mutable-span use with component writers and
            // with any operation that can acquire a new snapshot or lease.
            // This span is not a concurrent writer API. Runtime wide
            // admission remains disabled until runtime mutations use the
            // sidecar's multi-role preflight instead.
            return backing->plane_words(plane);
        }
        return backing->unversioned_planes[plane];
    }
    if (uses_inline_storage()) {
        const auto words = word_count(width());
        if (plane == 0U) {
            return { storage_.inline_value.aval.data(), words };
        }
        if (plane == 1U) {
            return { storage_.inline_value.bval.data(), words };
        }
        return plane == 2U
            ? std::span<std::uint64_t> { &inline_logic9_plane2(), 1U }
            : std::span<std::uint64_t> { &inline_logic9_plane3(), 1U };
    }
    ensure_unique_wide();
    if (plane == 0U) {
        return wide_storage()->aval;
    }
    if (plane == 1U) {
        return wide_storage()->bval;
    }
    return plane == 2U
        ? std::span<std::uint64_t> { wide_storage()->logic9_plane2 }
        : std::span<std::uint64_t> { wide_storage()->logic9_plane3 };
}

void PackedLogic4::materialize_plane_snapshot()
{
    if (!has_plane_snapshot()) {
        return;
    }
    const auto width_bits = width_and_logic9_ & ~plane_backing_mask;
    const auto current_width = width();
    const auto snapshot = extra_storage().plane_snapshot;
    if (uses_inline_storage()) {
        InlineStorage value;
        std::ranges::copy(snapshot_plane_words(0U), value.aval.begin());
        std::ranges::copy(snapshot_plane_words(1U), value.bval.begin());
        if (is_logic9()) {
            value.aval[1U] = snapshot_plane_words(2U).front();
            value.bval[1U] = snapshot_plane_words(3U).front();
        }
        release_plane_snapshot();
        std::destroy_at(&extra_storage().plane_snapshot);
        std::destroy_at(&storage_.indirect);
        std::construct_at(&storage_.inline_value, value);
        width_and_logic9_ = width_bits;
        plane_snapshot_ = false;
        return;
    }

    auto replacement = std::make_shared<WideStorage>(word_count(current_width));
    if (is_logic9()) {
        replacement->logic9_plane2.resize(word_count(current_width));
        replacement->logic9_plane3.resize(word_count(current_width));
    }
    for (std::size_t plane = 0U; plane < (is_logic9() ? 4U : 2U); ++plane) {
        const auto source = snapshot_plane_words(plane);
        auto& destination = plane == 0U ? replacement->aval
            : plane == 1U ? replacement->bval
            : plane == 2U ? replacement->logic9_plane2
                          : replacement->logic9_plane3;
        std::ranges::copy(source, destination.begin());
    }
    release_plane_snapshot();
    std::destroy_at(&extra_storage().plane_snapshot);
    std::construct_at(&extra_storage().wide, std::move(replacement));
    width_and_logic9_ = width_bits;
    plane_snapshot_ = false;
}

void PackedLogic4::ensure_unique_wide()
{
    if (has_plane_snapshot()) {
        materialize_plane_snapshot();
    }
    if (has_live_plane_backing()) {
        return;
    }
    if (uses_inline_storage()) {
        return;
    }
    if (!wide_storage()) {
        wide_storage() = std::make_shared<WideStorage>(word_count(width()));
        return;
    }
    if (wide_storage().use_count() == 1) {
        return;
    }
    wide_storage() = std::make_shared<WideStorage>(*wide_storage());
}

bool PackedLogic4::try_assign_wide_logic4_word_planes_noalloc(
    const std::span<const std::uint64_t> aval,
    const std::span<const std::uint64_t> bval) noexcept
{
    // Inline Logic4 widths through 128 bits use fixed in-object planes. They
    // need no reusable heap value, and reading wide_storage() would inspect the
    // inactive union member.
    if (uses_inline_storage() || is_logic9() || has_external_planes()
        || plane_snapshot_ || !wide_storage()
        || wide_storage().use_count() != 1U) {
        return false;
    }

    const auto words = width() / bits_per_word
        + (width() % bits_per_word == 0U ? 0U : 1U);
    auto& storage = *wide_storage();
    if (aval.size() != words || bval.size() != words
        || storage.aval.size() != words || storage.bval.size() != words) {
        return false;
    }

    const auto destination_aval
        = std::span<const std::uint64_t> { storage.aval };
    const auto destination_bval
        = std::span<const std::uint64_t> { storage.bval };
    const auto exact_aval_alias = exact_word_span(aval, destination_aval);
    const auto exact_bval_alias = exact_word_span(bval, destination_bval);
    if ((word_spans_overlap(aval, destination_aval) && !exact_aval_alias)
        || word_spans_overlap(aval, destination_bval)
        || word_spans_overlap(bval, destination_aval)
        || (word_spans_overlap(bval, destination_bval)
            && !exact_bval_alias)) {
        return false;
    }

    if (!exact_aval_alias) {
        std::copy(aval.begin(), aval.end(), storage.aval.begin());
    }
    if (!exact_bval_alias) {
        std::copy(bval.begin(), bval.end(), storage.bval.begin());
    }
    const auto remainder = width() % bits_per_word;
    if (remainder != 0U) {
        const auto mask = (std::uint64_t { 1 } << remainder) - 1U;
        storage.aval.back() &= mask;
        storage.bval.back() &= mask;
    }
    return true;
}

bool operator==(
    const PackedLogic4& left,
    const PackedLogic4& right) noexcept
{
    if (&left == &right) {
        return true;
    }
    if (left.width() != right.width() || left.is_logic9() != right.is_logic9()) {
        return false;
    }
    const auto plane_count = left.is_logic9() ? 4U : 2U;
    for (std::size_t plane = 0U; plane < plane_count; ++plane) {
        const auto lhs = plane == 0U ? left.aval_words()
            : plane == 1U ? left.bval_words()
                          : left.logic9_plane(plane);
        const auto rhs = plane == 0U ? right.aval_words()
            : plane == 1U ? right.bval_words()
                          : right.logic9_plane(plane);
        if (!std::ranges::equal(lhs, rhs)) {
            return false;
        }
    }
    return true;
}

PackedLogic4 PackedLogic4::from_msb_string(std::string_view value)
{
    PackedLogic4 result(value.size(), Logic4::zero);
    if (value.empty()) {
        return result;
    }
    // Planes are written a word at a time (0: a0 b0, 1: a1 b0, Z: a0 b1,
    // X: a1 b1), not bit by bit.
    const auto aval = result.mutable_aval_words();
    const auto bval = result.mutable_bval_words();
    for (std::size_t word = 0; word < aval.size(); ++word) {
        std::uint64_t a { };
        std::uint64_t b { };
        const auto first = word * bits_per_word;
        const auto last = std::min(value.size(), first + bits_per_word);
        for (std::size_t offset = first; offset < last; ++offset) {
            const auto parsed = parse_logic4(value[value.size() - offset - 1]);
            if (!parsed) {
                throw std::invalid_argument(
                    "four-state vector contains an invalid digit");
            }
            const auto bit = std::uint64_t { 1 } << (offset - first);
            switch (*parsed) {
            case Logic4::zero:
                break;
            case Logic4::one:
                a |= bit;
                break;
            case Logic4::z:
                b |= bit;
                break;
            case Logic4::x:
                a |= bit;
                b |= bit;
                break;
            }
        }
        aval[word] = a;
        bval[word] = b;
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
    result.inline_aval() = aval;
    result.inline_bval() = bval;
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
    result.promote_to_logic9();
    std::ranges::copy(plane0, result.mutable_logic9_plane(0).begin());
    std::ranges::copy(plane1, result.mutable_logic9_plane(1).begin());
    std::ranges::copy(plane2, result.mutable_logic9_plane(2).begin());
    std::ranges::copy(plane3, result.mutable_logic9_plane(3).begin());
    const auto result_plane0 = result.mutable_logic9_plane(0);
    const auto result_plane1 = result.mutable_logic9_plane(1);
    const auto result_plane2 = result.mutable_logic9_plane(2);
    const auto result_plane3 = result.mutable_logic9_plane(3);
    for (std::size_t word = 0; word < words; ++word) {
        const auto remaining_width = width - word * bits_per_word;
        auto value = Logic9Word {
            std::min(remaining_width, bits_per_word),
            { result_plane0[word], result_plane1[word],
                result_plane2[word], result_plane3[word] }
        };
        value.normalize_invalid_codes_to_x();
        result_plane0[word] = value.planes[0];
        result_plane1[word] = value.planes[1];
        result_plane2[word] = value.planes[2];
        result_plane3[word] = value.planes[3];
    }
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
    result.set_logic9(true);
    auto canonical_value = value;
    canonical_value.normalize_invalid_codes_to_x();
    result.inline_aval() = canonical_value.planes[0];
    result.inline_bval() = canonical_value.planes[1];
    result.inline_logic9_plane2() = canonical_value.planes[2];
    result.inline_logic9_plane3() = canonical_value.planes[3];
    result.mask_unused_bits();
    return result;
}

void PackedLogic4::assign_logic9_word(const Logic9Word& source)
{
    if (!is_logic9() || width() == 0U || width() > bits_per_word
        || source.width != width()) {
        throw std::invalid_argument(
            "nine-state word assignment requires a matching single-word value");
    }
    auto canonical_source = source;
    canonical_source.normalize_invalid_codes_to_x();
    for (std::size_t plane = 0U; plane < 4U; ++plane) {
        mutable_logic9_plane(plane).front() = canonical_source.planes[plane];
    }
    mask_unused_bits();
}

void PackedLogic4::insert_masked_logic9_word(
    const Logic9Word& source,
    std::uint64_t mask)
{
    if (!is_logic9() || width() == 0U || width() > bits_per_word
        || source.width != width()) {
        throw std::invalid_argument(
            "masked nine-state word assignment requires a matching single-word value");
    }
    mask &= final_word_mask(width());
    auto canonical_source = source;
    canonical_source.normalize_invalid_codes_to_x();
    for (std::size_t plane = 0U; plane < 4U; ++plane) {
        auto words = mutable_logic9_plane(plane);
        words.front() = (words.front() & ~mask)
            | (canonical_source.planes[plane] & mask);
    }
}

bool PackedLogic4::matches_masked_logic9_word(
    const Logic9Word& source,
    std::uint64_t mask) const
{
    if (!is_logic9() || width() == 0U || width() > bits_per_word
        || source.width != width()) {
        return false;
    }
    mask &= final_word_mask(width());
    auto canonical_source = source;
    canonical_source.normalize_invalid_codes_to_x();
    for (std::size_t plane = 0U; plane < 4U; ++plane) {
        if (((logic9_plane(plane).front() ^ canonical_source.planes[plane])
                & mask) != 0U) {
            return false;
        }
    }
    return true;
}

std::span<const std::uint64_t>
PackedLogic4::aval_words() const noexcept
{
    if (width() == 0) {
        return { };
    }
    if (has_live_plane_backing()) {
        return plane_backing()->plane_words(0U);
    }
    if (has_plane_snapshot()) {
        return snapshot_plane_words(0U);
    }
    if (uses_inline_storage()) {
        return { storage_.inline_value.aval.data(), word_count(width()) };
    }
    return wide_storage()->aval;
}

std::span<const std::uint64_t>
PackedLogic4::bval_words() const noexcept
{
    if (width() == 0) {
        return { };
    }
    if (has_live_plane_backing()) {
        return plane_backing()->plane_words(1U);
    }
    if (has_plane_snapshot()) {
        return snapshot_plane_words(1U);
    }
    if (uses_inline_storage()) {
        return { storage_.inline_value.bval.data(), word_count(width()) };
    }
    return wide_storage()->bval;
}

std::span<const std::uint64_t>
PackedLogic4::logic9_plane_words(const std::size_t plane) const noexcept
{
    if (!is_logic9() || plane >= 4) {
        return { };
    }
    return logic9_plane(plane);
}

std::span<std::uint64_t>
PackedLogic4::mutable_aval_words()
{
    if (width() == 0) {
        return { };
    }
    return prepare_plane_storage_for_write(0U);
}

std::span<std::uint64_t>
PackedLogic4::mutable_bval_words()
{
    if (width() == 0) {
        return { };
    }
    return prepare_plane_storage_for_write(1U);
}

std::optional<std::uint64_t>
PackedLogic4::known_unsigned_value() const noexcept
{
    if (width() == 0 || is_logic9()) {
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
    if (width() == 0 || is_logic9()) {
        return std::nullopt;
    }
    const auto aval = aval_words();
    const auto bval = bval_words();
    if (std::ranges::any_of(bval, [](const auto word) { return word != 0; })) {
        return std::nullopt;
    }

    auto low = aval.front();
    if (width() < bits_per_word) {
        const auto sign = UINT64_C(1) << (width() - 1U);
        if ((low & sign) != 0) {
            low |= ~final_word_mask(width());
        }
        return std::bit_cast<std::int64_t>(low);
    }

    const auto negative = (low & (UINT64_C(1) << 63U)) != 0;
    for (std::size_t index = 1; index < aval.size(); ++index) {
        const auto mask = index + 1U == aval.size()
            ? final_word_mask(width())
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
    if (width() == 0 || width() > bits_per_word) {
        throw std::invalid_argument(
            "four-state word width must be between 1 and 64");
    }
    if (is_logic9()) {
        throw std::invalid_argument(
            "an exact nine-state value has no lossless aval/bval word");
    }
    const auto aval = aval_words();
    const auto bval = bval_words();
    return { width(), aval.front(), bval.front() };
}

void PackedLogic4::assign_word(const Logic4Word& source)
{
    if (is_logic9() || width() == 0U || width() > bits_per_word
        || source.width != width()) {
        throw std::invalid_argument(
            "assigned four-state word must match a single-word Logic4 value");
    }
    const auto mask = final_word_mask(width());
    mutable_aval_words().front() = source.aval & mask;
    mutable_bval_words().front() = source.bval & mask;
}

Logic4 PackedLogic4::get(std::size_t index) const
{
    check_index(index, width());
    if (is_logic9()) {
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
    check_index(index, width());
    if (!is_logic9()) {
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
    check_index(index, width());
    if (is_logic9()) {
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
    check_index(index, width());
    if (!is_logic9()) {
        promote_to_logic9();
    }
    const auto encoded
        = static_cast<std::uint8_t>(canonical_logic9_value(value));
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
        || offset > width() || source.width > width() - offset) {
        throw std::invalid_argument(
            "insert word range is outside its target value");
    }
    if (!is_logic9()) {
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
        || offset > width() || source.width > width() - offset) {
        throw std::invalid_argument(
            "word comparison range is outside its target value");
    }
    if (is_logic9()) {
        return false;
    }
    const auto mask = final_word_mask(source.width);
    if (width() <= bits_per_word) {
        return ((aval_words().front() >> offset) & mask)
                == (source.aval & mask)
            && ((bval_words().front() >> offset) & mask)
                == (source.bval & mask);
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
        || offset > width() || source.width > width() - offset) {
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
    if (is_logic9()) {
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
        || offset > width() || source.width > width() - offset) {
        throw std::invalid_argument(
            "masked word comparison range is outside its target value");
    }
    if (is_logic9()) {
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
    if (source.width() == 0U || offset > width()
        || source.width() > width() - offset) {
        throw std::invalid_argument(
            "insert range is outside its target value");
    }
    if (this == &source) {
        const auto stable_source = source;
        insert_bits(stable_source, offset);
        return;
    }
    if (!is_logic9() && !source.is_logic9()) {
        copy_bit_range(
            mutable_aval_words(), source.aval_words(), offset, source.width());
        copy_bit_range(
            mutable_bval_words(), source.bval_words(), offset, source.width());
        mask_unused_bits();
        return;
    }

    promote_to_logic9();
    if (source.is_logic9()) {
        for (std::size_t plane = 0; plane < 4U; ++plane) {
            copy_bit_range(mutable_logic9_plane(plane),
                source.logic9_plane(plane), offset, source.width());
        }
    } else {
        const auto promoted_source = source.promoted_to_logic9();
        for (std::size_t plane = 0; plane < 4U; ++plane) {
            copy_bit_range(mutable_logic9_plane(plane),
                promoted_source.logic9_plane(plane), offset,
                source.width());
        }
    }
    mask_unused_bits();
}

PackedLogic4 PackedLogic4::extract_bits(
    const std::size_t offset,
    const std::size_t width) const
{
    if (width == 0U || offset > this->width()
        || width > this->width() - offset) {
        throw std::invalid_argument(
            "extract range offset " + std::to_string(offset)
            + " width " + std::to_string(width)
            + " is outside source width "
            + std::to_string(this->width()));
    }
    PackedLogic4 result(width, Logic4::zero);
    if (!is_logic9()) {
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
    if (is_logic9()) {
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
    if (!is_logic9()) {
        promote_to_logic9();
    }
    const auto encoded
        = static_cast<std::uint8_t>(canonical_logic9_value(value));
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
    if (width() == 0 || width() > bits_per_word) {
        throw std::invalid_argument(
            "nine-state word width must be between 1 and 64");
    }
    Logic9Word result { width() };
    if (is_logic9()) {
        for (std::size_t plane = 0U; plane < 4U; ++plane) {
            result.planes[plane] = logic9_plane(plane).front();
        }
    } else {
        for (std::size_t index = 0; index < width(); ++index) {
            const auto encoded
                = static_cast<std::uint8_t>(to_logic9(get(index)));
            for (std::size_t plane = 0; plane < 4; ++plane) {
                if (((encoded >> plane) & 1U) != 0) {
                    result.planes[plane] |= std::uint64_t { 1 } << index;
                }
            }
        }
    }
    result.normalize_invalid_codes_to_x();
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
    std::string result(width(), 'X');
    const auto aval = aval_words();
    const auto bval = bval_words();
    if (!is_logic9()) {
        for (std::size_t index = 0; index < width(); ++index) {
            const auto mask = std::uint64_t{1} << (index % bits_per_word);
            const auto word = index / bits_per_word;
            result[width() - index - 1] = (bval[word] & mask) != 0U
                ? ((aval[word] & mask) != 0U ? 'X' : 'Z')
                : ((aval[word] & mask) != 0U ? '1' : '0');
        }
        return result;
    }

    constexpr std::array digits{'U', 'X', '0', '1', 'Z', 'W', 'L', 'H', '-'};
    const auto plane2 = logic9_plane(2);
    const auto plane3 = logic9_plane(3);
    for (std::size_t index = 0; index < width(); ++index) {
        const auto mask = std::uint64_t{1} << (index % bits_per_word);
        const auto word = index / bits_per_word;
        const auto encoded = static_cast<std::size_t>(
            ((aval[word] & mask) != 0U ? 1U : 0U)
            | ((bval[word] & mask) != 0U ? 2U : 0U)
            | ((plane2[word] & mask) != 0U ? 4U : 0U)
            | ((plane3[word] & mask) != 0U ? 8U : 0U));
        result[width() - index - 1]
            = digits[encoded <= static_cast<std::size_t>(Logic9::dont_care)
                    ? encoded
                    : static_cast<std::size_t>(Logic9::x)];
    }
    return result;
}

void PackedLogic4::promote_to_logic9()
{
    if (is_logic9()) {
        return;
    }
    if (has_plane_backing()) {
        throw std::logic_error {
            "cannot promote a component-backed Logic4 slot to Logic9"
        };
    }
    if (has_plane_snapshot()) {
        materialize_plane_snapshot();
    }
    const auto words = word_count(width());
    if (width() <= bits_per_word) {
        const auto aval = inline_aval();
        const auto bval = inline_bval();
        set_logic9(true);
        inline_aval() = aval;
        inline_bval() = ~bval;
        inline_logic9_plane2() = ~aval & bval;
        inline_logic9_plane3() = 0U;
        mask_unused_bits();
        return;
    }
    if (uses_inline_storage()) {
        // Allocate every destination plane before replacing either inline
        // source plane. A failure leaves the original Logic4 value intact.
        auto replacement = std::make_shared<WideStorage>(words);
        replacement->logic9_plane2.resize(words);
        replacement->logic9_plane3.resize(words);
        for (std::size_t word = 0U; word < words; ++word) {
            const auto aval = storage_.inline_value.aval[word];
            const auto bval = storage_.inline_value.bval[word];
            replacement->aval[word] = aval;
            replacement->bval[word] = ~bval;
            replacement->logic9_plane2[word] = ~aval & bval;
        }
        std::destroy_at(&storage_.inline_value);
        std::construct_at(&storage_.indirect);
        std::construct_at(&extra_storage().wide, std::move(replacement));
    } else {
        std::vector<std::uint64_t> plane2(words);
        std::vector<std::uint64_t> plane3(words);
        ensure_unique_wide();
        auto& value = *wide_storage();
        for (std::size_t word = 0U; word < words; ++word) {
            plane2[word] = ~value.aval[word] & value.bval[word];
            value.bval[word] = ~value.bval[word];
        }
        value.logic9_plane2 = std::move(plane2);
        value.logic9_plane3 = std::move(plane3);
    }
    set_logic9(true);
    mask_unused_bits();
}

std::span<const std::uint64_t>
PackedLogic4::logic9_plane(const std::size_t index) const noexcept
{
    if (width() == 0 || index >= 4) {
        return { };
    }
    if (has_live_plane_backing()) {
        return plane_backing()->plane_words(index);
    }
    if (has_plane_snapshot()) {
        return snapshot_plane_words(index);
    }
    if (index == 0) {
        return aval_words();
    }
    if (index == 1) {
        return bval_words();
    }
    if (width() <= bits_per_word) {
        return index == 2
            ? std::span<const std::uint64_t> {
                  &inline_logic9_plane2(), 1
              }
            : std::span<const std::uint64_t> { &inline_logic9_plane3(), 1 };
    }
    return index == 2
        ? std::span<const std::uint64_t> { wide_storage()->logic9_plane2 }
        : std::span<const std::uint64_t> { wide_storage()->logic9_plane3 };
}

std::span<std::uint64_t>
PackedLogic4::mutable_logic9_plane(
    const std::size_t index)
{
    if (width() == 0 || index >= 4) {
        return { };
    }
    return prepare_plane_storage_for_write(index);
}

void PackedLogic4::mask_unused_bits()
{
    mask_last(mutable_aval_words(), width());
    mask_last(mutable_bval_words(), width());
    if (is_logic9()) {
        mask_last(mutable_logic9_plane(2), width());
        mask_last(mutable_logic9_plane(3), width());
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
    const auto encoded
        = static_cast<std::uint8_t>(canonical_logic9_value(value));
    for (std::size_t plane = 0; plane < planes_.size(); ++plane) {
        write_bit(planes_[plane], index, ((encoded >> plane) & 1U) != 0);
    }
}

void PackedLogic9::fill(Logic9 value) noexcept
{
    const auto encoded
        = static_cast<std::uint8_t>(canonical_logic9_value(value));
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
