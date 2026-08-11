// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/packed_value.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace fsim::runtime {
namespace {

constexpr std::size_t bits_per_word = 64;

[[nodiscard]] std::size_t word_count(std::size_t width) {
  if (width > std::numeric_limits<std::size_t>::max() -
                  (bits_per_word - 1)) {
    throw std::length_error("packed value width is too large");
  }
  return (width + bits_per_word - 1) / bits_per_word;
}

void check_index(std::size_t index, std::size_t width) {
  if (index >= width) {
    throw std::out_of_range("packed value bit index is out of range");
  }
}

[[nodiscard]] constexpr std::uint64_t final_word_mask(std::size_t width) {
  const auto remainder = width % bits_per_word;
  return remainder == 0 ? ~std::uint64_t{0}
                        : (std::uint64_t{1} << remainder) - 1;
}

void mask_last(std::span<std::uint64_t> words, std::size_t width) noexcept {
  if (!words.empty()) {
    words.back() &= final_word_mask(width);
  }
}

[[nodiscard]] bool read_bit(std::span<const std::uint64_t> words,
                            std::size_t index) noexcept {
  return ((words[index / bits_per_word] >> (index % bits_per_word)) & 1U) != 0;
}

void write_bit(std::span<std::uint64_t> words, std::size_t index,
               bool value) noexcept {
  const auto mask = std::uint64_t{1} << (index % bits_per_word);
  auto &word = words[index / bits_per_word];
  if (value) {
    word |= mask;
  } else {
    word &= ~mask;
  }
}

} // namespace

PackedBit2::PackedBit2(std::size_t width, bool initial)
    : width_(width), words_(word_count(width), initial ? ~std::uint64_t{0} : 0) {
  mask_unused_bits();
}

PackedBit2 PackedBit2::from_msb_string(std::string_view value) {
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

bool PackedBit2::get(std::size_t index) const {
  check_index(index, width_);
  return read_bit(words_, index);
}

void PackedBit2::set(std::size_t index, bool value) {
  check_index(index, width_);
  write_bit(words_, index, value);
}

void PackedBit2::fill(bool value) noexcept {
  std::fill(words_.begin(), words_.end(),
            value ? ~std::uint64_t{0} : std::uint64_t{0});
  mask_unused_bits();
}

std::string PackedBit2::to_msb_string() const {
  std::string result(width_, '0');
  for (std::size_t index = 0; index < width_; ++index) {
    result[width_ - index - 1] = get(index) ? '1' : '0';
  }
  return result;
}

void PackedBit2::mask_unused_bits() noexcept { mask_last(words_, width_); }

PackedLogic4::PackedLogic4(std::size_t width, Logic4 initial)
    : width_(width),
      aval_(width > bits_per_word ? word_count(width) : 0),
      bval_(width > bits_per_word ? word_count(width) : 0) {
  fill(initial);
}

PackedLogic4 PackedLogic4::from_msb_string(std::string_view value) {
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
    const std::string_view value) {
  PackedLogic4 result(value.size(), Logic4::zero);
  result.promote_to_logic9();
  for (std::size_t offset = 0; offset < value.size(); ++offset) {
    const auto parsed =
        parse_logic9(value[value.size() - offset - 1]);
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
    const std::uint64_t bval) {
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

PackedLogic4 PackedLogic4::from_logic9_word(
    const Logic9Word& value) {
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

std::span<const std::uint64_t>
PackedLogic4::aval_words() const noexcept {
  if (width_ == 0) {
    return {};
  }
  if (width_ <= bits_per_word) {
    return {&inline_aval_, 1};
  }
  return aval_;
}

std::span<const std::uint64_t>
PackedLogic4::bval_words() const noexcept {
  if (width_ == 0) {
    return {};
  }
  if (width_ <= bits_per_word) {
    return {&inline_bval_, 1};
  }
  return bval_;
}

std::span<std::uint64_t>
PackedLogic4::mutable_aval_words() noexcept {
  if (width_ == 0) {
    return {};
  }
  if (width_ <= bits_per_word) {
    return {&inline_aval_, 1};
  }
  return aval_;
}

std::span<std::uint64_t>
PackedLogic4::mutable_bval_words() noexcept {
  if (width_ == 0) {
    return {};
  }
  if (width_ <= bits_per_word) {
    return {&inline_bval_, 1};
  }
  return bval_;
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

Logic4Word PackedLogic4::low_word() const {
  if (width_ == 0 || width_ > bits_per_word) {
    throw std::invalid_argument(
        "four-state word width must be between 1 and 64");
  }
  if (logic9_) {
    throw std::invalid_argument(
        "an exact nine-state value has no lossless aval/bval word");
  }
  return {width_, inline_aval_, inline_bval_};
}

Logic4 PackedLogic4::get(std::size_t index) const {
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

Logic9 PackedLogic4::get_logic9(const std::size_t index) const {
  check_index(index, width_);
  if (!logic9_) {
    return to_logic9(get(index));
  }
  std::uint8_t encoded{};
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

void PackedLogic4::set(std::size_t index, Logic4 value) {
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
    const Logic9 value) {
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

void PackedLogic4::fill(Logic4 value) noexcept {
  if (logic9_) {
    const auto encoded =
        static_cast<std::uint8_t>(to_logic9(value));
    for (std::size_t plane = 0; plane < 4; ++plane) {
      auto words = mutable_logic9_plane(plane);
      std::fill(
          words.begin(),
          words.end(),
          ((encoded >> plane) & 1U) != 0
              ? ~std::uint64_t{0}
              : std::uint64_t{0});
    }
    mask_unused_bits();
    return;
  }
  const bool aval = value == Logic4::one || value == Logic4::x;
  const bool bval = value == Logic4::x || value == Logic4::z;
  const auto aval_words = mutable_aval_words();
  const auto bval_words = mutable_bval_words();
  std::fill(aval_words.begin(), aval_words.end(),
            aval ? ~std::uint64_t{0} : std::uint64_t{0});
  std::fill(bval_words.begin(), bval_words.end(),
            bval ? ~std::uint64_t{0} : std::uint64_t{0});
  mask_unused_bits();
}

void PackedLogic4::fill(const Logic9 value) {
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
            ? ~std::uint64_t{0}
            : std::uint64_t{0});
  }
  mask_unused_bits();
}

Logic9Word PackedLogic4::logic9_low_word() const {
  if (width_ == 0 || width_ > bits_per_word) {
    throw std::invalid_argument(
        "nine-state word width must be between 1 and 64");
  }
  Logic9Word result{width_};
  if (logic9_) {
    result.planes = {
        inline_aval_,
        inline_bval_,
        inline_logic9_plane2_,
        inline_logic9_plane3_};
    return result;
  }
  for (std::size_t index = 0; index < width_; ++index) {
    const auto encoded =
        static_cast<std::uint8_t>(to_logic9(get(index)));
    for (std::size_t plane = 0; plane < 4; ++plane) {
      if (((encoded >> plane) & 1U) != 0) {
        result.planes[plane] |= std::uint64_t{1} << index;
      }
    }
  }
  return result;
}

PackedLogic4 PackedLogic4::promoted_to_logic9() const {
  auto result = *this;
  result.promote_to_logic9();
  return result;
}

std::string PackedLogic4::to_msb_string() const {
  std::string result(width_, 'X');
  for (std::size_t index = 0; index < width_; ++index) {
    result[width_ - index - 1] =
        logic9_ ? to_char(get_logic9(index)) : to_char(get(index));
  }
  return result;
}

void PackedLogic4::promote_to_logic9() {
  if (logic9_) {
    return;
  }
  std::vector<Logic4> old_values;
  old_values.reserve(width_);
  for (std::size_t index = 0; index < width_; ++index) {
    old_values.push_back(get(index));
  }
  if (width_ > bits_per_word) {
    logic9_plane2_.assign(word_count(width_), 0);
    logic9_plane3_.assign(word_count(width_), 0);
  }
  logic9_ = true;
  for (std::size_t index = 0; index < width_; ++index) {
    set_logic9(index, to_logic9(old_values[index]));
  }
}

std::span<const std::uint64_t>
PackedLogic4::logic9_plane(const std::size_t index) const noexcept {
  if (width_ == 0 || index >= 4) {
    return {};
  }
  if (index == 0) {
    return aval_words();
  }
  if (index == 1) {
    return bval_words();
  }
  if (width_ <= bits_per_word) {
    return index == 2
        ? std::span<const std::uint64_t>{
              &inline_logic9_plane2_, 1}
        : std::span<const std::uint64_t>{
              &inline_logic9_plane3_, 1};
  }
  return index == 2
      ? std::span<const std::uint64_t>{logic9_plane2_}
      : std::span<const std::uint64_t>{logic9_plane3_};
}

std::span<std::uint64_t>
PackedLogic4::mutable_logic9_plane(
    const std::size_t index) noexcept {
  if (width_ == 0 || index >= 4) {
    return {};
  }
  if (index == 0) {
    return mutable_aval_words();
  }
  if (index == 1) {
    return mutable_bval_words();
  }
  if (width_ <= bits_per_word) {
    return index == 2
        ? std::span<std::uint64_t>{
              &inline_logic9_plane2_, 1}
        : std::span<std::uint64_t>{
              &inline_logic9_plane3_, 1};
  }
  return index == 2
      ? std::span<std::uint64_t>{logic9_plane2_}
      : std::span<std::uint64_t>{logic9_plane3_};
}

void PackedLogic4::mask_unused_bits() noexcept {
  mask_last(mutable_aval_words(), width_);
  mask_last(mutable_bval_words(), width_);
  if (logic9_) {
    mask_last(mutable_logic9_plane(2), width_);
    mask_last(mutable_logic9_plane(3), width_);
  }
}

PackedLogic9::PackedLogic9(std::size_t width, Logic9 initial)
    : width_(width) {
  const auto count = word_count(width);
  for (auto &plane : planes_) {
    plane.resize(count);
  }
  fill(initial);
}

PackedLogic9 PackedLogic9::from_msb_string(std::string_view value) {
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

Logic9 PackedLogic9::get(std::size_t index) const {
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

void PackedLogic9::set(std::size_t index, Logic9 value) {
  check_index(index, width_);
  const auto encoded = static_cast<std::uint8_t>(value);
  for (std::size_t plane = 0; plane < planes_.size(); ++plane) {
    write_bit(planes_[plane], index, ((encoded >> plane) & 1U) != 0);
  }
}

void PackedLogic9::fill(Logic9 value) noexcept {
  const auto encoded = static_cast<std::uint8_t>(value);
  for (std::size_t plane = 0; plane < planes_.size(); ++plane) {
    std::fill(planes_[plane].begin(), planes_[plane].end(),
              ((encoded >> plane) & 1U) != 0 ? ~std::uint64_t{0}
                                              : std::uint64_t{0});
  }
  mask_unused_bits();
}

std::span<const std::uint64_t>
PackedLogic9::plane(std::size_t index) const {
  if (index >= planes_.size()) {
    throw std::out_of_range("nine-state vector plane index is out of range");
  }
  return planes_[index];
}

std::string PackedLogic9::to_msb_string() const {
  std::string result(width_, 'U');
  for (std::size_t index = 0; index < width_; ++index) {
    result[width_ - index - 1] = to_char(get(index));
  }
  return result;
}

void PackedLogic9::mask_unused_bits() noexcept {
  for (auto &plane : planes_) {
    mask_last(plane, width_);
  }
}

PackedLogic4 collapse_to_logic4(const PackedLogic9 &value) {
  PackedLogic4 result(value.width(), Logic4::zero);
  for (std::size_t index = 0; index < value.width(); ++index) {
    result.set(index, to_logic4(value.get(index)));
  }
  return result;
}

PackedLogic4 collapse_to_logic4(const PackedLogic4& value) {
  if (!value.is_logic9()) {
    return value;
  }
  PackedLogic4 result(value.width(), Logic4::zero);
  for (std::size_t index = 0; index < value.width(); ++index) {
    result.set(index, to_logic4(value.get_logic9(index)));
  }
  return result;
}

PackedLogic9 expand_to_logic9(const PackedLogic4 &value) {
  PackedLogic9 result(value.width(), Logic9::zero);
  for (std::size_t index = 0; index < value.width(); ++index) {
    result.set(index, value.get_logic9(index));
  }
  return result;
}

PackedLogic4 resolve(std::span<const PackedLogic4> drivers) {
  if (drivers.empty()) {
    return PackedLogic4{};
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
  PackedLogic4 result(width, Logic4::z);
  if (logic9) {
    result.fill(Logic9::z);
  }
  for (const auto &driver : drivers) {
    if (driver.width() != width) {
      throw std::invalid_argument("cannot resolve drivers of different widths");
    }
    for (std::size_t index = 0; index < width; ++index) {
      if (logic9) {
        result.set_logic9(
            index,
            resolve(
                result.get_logic9(index),
                driver.get_logic9(index)));
      } else {
        result.set(
            index,
            resolve(result.get(index), driver.get(index)));
      }
    }
  }
  return result;
}

PackedLogic9 resolve(std::span<const PackedLogic9> drivers) {
  if (drivers.empty()) {
    return PackedLogic9{};
  }
  if (drivers.size() == 1) {
    return drivers.front();
  }
  const auto width = drivers.front().width();
  PackedLogic9 result(width, Logic9::z);
  for (const auto &driver : drivers) {
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
