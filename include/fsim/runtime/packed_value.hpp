// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/logic.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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
  std::size_t width{};
  std::uint64_t aval{};
  std::uint64_t bval{};

  friend bool operator==(const Logic4Word&, const Logic4Word&) = default;
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

  [[nodiscard]] std::span<const std::uint64_t> words() const noexcept {
    return words_;
  }
  [[nodiscard]] std::span<std::uint64_t> words() noexcept { return words_; }
  [[nodiscard]] std::string to_msb_string() const;

  friend bool operator==(const PackedBit2 &, const PackedBit2 &) = default;

private:
  void mask_unused_bits() noexcept;

  std::size_t width_{};
  std::vector<std::uint64_t> words_;
};

/// A packed four-state vector using the conventional aval/bval encoding:
/// 0=00, 1=10, X=11, Z=01.
class PackedLogic4 {
public:
  explicit PackedLogic4(std::size_t width = 0,
                        Logic4 initial = Logic4::x);

  [[nodiscard]] static PackedLogic4 from_msb_string(std::string_view value);
  [[nodiscard]] static PackedLogic4
  from_aval_bval(std::size_t width, std::uint64_t aval,
                 std::uint64_t bval);

  [[nodiscard]] std::size_t width() const noexcept { return width_; }
  [[nodiscard]] bool empty() const noexcept { return width_ == 0; }
  [[nodiscard]] Logic4 get(std::size_t index) const;
  void set(std::size_t index, Logic4 value);
  void fill(Logic4 value) noexcept;

  [[nodiscard]] std::span<const std::uint64_t>
  aval_words() const noexcept;
  [[nodiscard]] std::span<const std::uint64_t>
  bval_words() const noexcept;
  [[nodiscard]] Logic4Word low_word() const;
  [[nodiscard]] std::string to_msb_string() const;

  friend bool operator==(const PackedLogic4 &, const PackedLogic4 &) = default;

private:
  [[nodiscard]] std::span<std::uint64_t>
  mutable_aval_words() noexcept;
  [[nodiscard]] std::span<std::uint64_t>
  mutable_bval_words() noexcept;
  void mask_unused_bits() noexcept;

  std::size_t width_{};
  std::uint64_t inline_aval_{};
  std::uint64_t inline_bval_{};
  std::vector<std::uint64_t> aval_;
  std::vector<std::uint64_t> bval_;
};

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

  friend bool operator==(const PackedLogic9 &, const PackedLogic9 &) = default;

private:
  void mask_unused_bits() noexcept;

  std::size_t width_{};
  std::array<std::vector<std::uint64_t>, 4> planes_;
};

[[nodiscard]] PackedLogic4 collapse_to_logic4(const PackedLogic9 &value);
[[nodiscard]] PackedLogic9 expand_to_logic9(const PackedLogic4 &value);

[[nodiscard]] PackedLogic4 resolve(std::span<const PackedLogic4> drivers);
[[nodiscard]] PackedLogic9 resolve(std::span<const PackedLogic9> drivers);

} // namespace fsim::runtime
