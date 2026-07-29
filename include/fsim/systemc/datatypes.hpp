// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/core.hpp"

namespace sc_dt {

class sc_logic final {
public:
    enum value_t : std::uint8_t { Log_0 = 0, Log_1 = 1, Log_Z = 2, Log_X = 3 };

    constexpr sc_logic() noexcept = default;
    constexpr sc_logic(const bool value) noexcept : value_(value ? Log_1 : Log_0) {}
    constexpr sc_logic(const value_t value) : value_(validate(value)) {}
    constexpr explicit sc_logic(const char value) : value_(from_char(value)) {}

    constexpr sc_logic& operator=(const bool value) noexcept {
        value_ = value ? Log_1 : Log_0;
        return *this;
    }

    constexpr sc_logic& operator=(const value_t value) {
        value_ = validate(value);
        return *this;
    }

    constexpr sc_logic& operator=(const char value) {
        value_ = from_char(value);
        return *this;
    }

    [[nodiscard]] constexpr value_t value() const noexcept { return value_; }
    [[nodiscard]] constexpr char to_char() const noexcept {
        constexpr std::array chars{'0', '1', 'Z', 'X'};
        const auto index = static_cast<std::size_t>(value_);
        return index < chars.size() ? chars[index] : 'X';
    }
    [[nodiscard]] constexpr bool is_01() const noexcept {
        return value_ == Log_0 || value_ == Log_1;
    }
    [[nodiscard]] constexpr bool to_bool() const {
        if (!is_01()) {
            throw std::logic_error{"X or Z cannot be converted to bool"};
        }
        return value_ == Log_1;
    }

    [[nodiscard]] friend constexpr sc_logic operator~(
        const sc_logic value) noexcept {
        if (value.value_ == Log_0) {
            return sc_logic{Log_1};
        }
        if (value.value_ == Log_1) {
            return sc_logic{Log_0};
        }
        return sc_logic{Log_X};
    }

    [[nodiscard]] friend constexpr sc_logic operator&(
        const sc_logic lhs,
        const sc_logic rhs) noexcept {
        if (lhs.value_ == Log_0 || rhs.value_ == Log_0) {
            return sc_logic{Log_0};
        }
        if (lhs.value_ == Log_1 && rhs.value_ == Log_1) {
            return sc_logic{Log_1};
        }
        return sc_logic{Log_X};
    }

    [[nodiscard]] friend constexpr sc_logic operator|(
        const sc_logic lhs,
        const sc_logic rhs) noexcept {
        if (lhs.value_ == Log_1 || rhs.value_ == Log_1) {
            return sc_logic{Log_1};
        }
        if (lhs.value_ == Log_0 && rhs.value_ == Log_0) {
            return sc_logic{Log_0};
        }
        return sc_logic{Log_X};
    }

    [[nodiscard]] friend constexpr sc_logic operator^(
        const sc_logic lhs,
        const sc_logic rhs) noexcept {
        if (!lhs.is_01() || !rhs.is_01()) {
            return sc_logic{Log_X};
        }
        return sc_logic{
            lhs.value_ == rhs.value_ ? Log_0 : Log_1};
    }

    constexpr sc_logic& operator&=(const sc_logic rhs) noexcept {
        return *this = *this & rhs;
    }

    constexpr sc_logic& operator|=(const sc_logic rhs) noexcept {
        return *this = *this | rhs;
    }

    constexpr sc_logic& operator^=(const sc_logic rhs) noexcept {
        return *this = *this ^ rhs;
    }

    friend constexpr auto operator<=>(const sc_logic&, const sc_logic&) noexcept = default;

private:
    static constexpr value_t validate(const value_t value) {
        if (static_cast<std::uint8_t>(value)
            > static_cast<std::uint8_t>(Log_X)) {
            throw std::invalid_argument{"invalid sc_logic value"};
        }
        return value;
    }

    static constexpr value_t from_char(const char value) {
        switch (value) {
        case '0':
            return Log_0;
        case '1':
            return Log_1;
        case 'z':
        case 'Z':
            return Log_Z;
        case 'x':
        case 'X':
            return Log_X;
        default:
            throw std::invalid_argument{"invalid sc_logic character"};
        }
    }

    value_t value_{Log_X};
};

template <int Width>
class sc_bv final {
    static_assert(Width > 0, "sc_bv width must be positive");

public:
    class bit_reference final {
    public:
        constexpr bit_reference(
            sc_bv& owner,
            const std::size_t index) noexcept
            : owner_(&owner), index_(index) {}

        bit_reference& operator=(const bool value) {
            owner_->bits_.set(index_, value);
            return *this;
        }

        bit_reference& operator=(const bit_reference& value) {
            return *this = static_cast<bool>(value);
        }

        [[nodiscard]] operator bool() const {
            return owner_->bits_.test(index_);
        }

        void flip() {
            owner_->bits_.flip(index_);
        }

    private:
        sc_bv* owner_;
        std::size_t index_;
    };

    sc_bv() = default;
    explicit sc_bv(const char* text) { assign(text); }
    explicit sc_bv(const std::string_view text) { assign(text); }
    constexpr sc_bv(const std::uint64_t value) noexcept {
        assign(value);
    }

    void assign(const std::string_view text) {
        if (text.size() != static_cast<std::size_t>(Width)) {
            throw std::invalid_argument{"sc_bv text has the wrong width"};
        }
        for (int i = 0; i < Width; ++i) {
            const auto c = text[static_cast<std::size_t>(Width - 1 - i)];
            if (c != '0' && c != '1') {
                throw std::invalid_argument{"sc_bv accepts only 0 and 1"};
            }
            bits_.set(static_cast<std::size_t>(i), c == '1');
        }
    }

    constexpr void assign(const std::uint64_t value) noexcept {
        for (int bit = 0; bit < Width; ++bit) {
            bits_.set(
                static_cast<std::size_t>(bit),
                bit < 64
                    && ((value >> static_cast<unsigned>(bit)) & 1U) != 0);
        }
    }

    [[nodiscard]] static constexpr int length() noexcept {
        return Width;
    }

    [[nodiscard]] bool operator[](const std::size_t index) const {
        check_index(index);
        return bits_.test(index);
    }

    [[nodiscard]] bit_reference operator[](
        const std::size_t index) {
        check_index(index);
        return bit_reference{*this, index};
    }

    [[nodiscard]] std::uint64_t to_uint64() const noexcept {
        std::uint64_t result = 0;
        constexpr auto limit = Width < 64 ? Width : 64;
        for (int bit = 0; bit < limit; ++bit) {
            if (bits_.test(static_cast<std::size_t>(bit))) {
                result |= std::uint64_t{1}
                    << static_cast<unsigned>(bit);
            }
        }
        return result;
    }

    [[nodiscard]] std::string to_string() const {
        std::string result(static_cast<std::size_t>(Width), '0');
        for (int i = 0; i < Width; ++i) {
            result[static_cast<std::size_t>(Width - 1 - i)] =
                bits_.test(static_cast<std::size_t>(i)) ? '1' : '0';
        }
        return result;
    }

    sc_bv& operator&=(const sc_bv& rhs) noexcept {
        bits_ &= rhs.bits_;
        return *this;
    }

    sc_bv& operator|=(const sc_bv& rhs) noexcept {
        bits_ |= rhs.bits_;
        return *this;
    }

    sc_bv& operator^=(const sc_bv& rhs) noexcept {
        bits_ ^= rhs.bits_;
        return *this;
    }

    sc_bv& operator<<=(const std::size_t amount) noexcept {
        bits_ <<= amount;
        return *this;
    }

    sc_bv& operator>>=(const std::size_t amount) noexcept {
        bits_ >>= amount;
        return *this;
    }

    [[nodiscard]] bool and_reduce() const noexcept {
        return bits_.all();
    }

    [[nodiscard]] bool or_reduce() const noexcept {
        return bits_.any();
    }

    [[nodiscard]] bool xor_reduce() const noexcept {
        return bits_.count() % 2U != 0;
    }

    [[nodiscard]] friend sc_bv operator~(sc_bv value) noexcept {
        value.bits_.flip();
        return value;
    }

    [[nodiscard]] friend sc_bv operator&(
        sc_bv lhs,
        const sc_bv& rhs) noexcept {
        return lhs &= rhs;
    }

    [[nodiscard]] friend sc_bv operator|(
        sc_bv lhs,
        const sc_bv& rhs) noexcept {
        return lhs |= rhs;
    }

    [[nodiscard]] friend sc_bv operator^(
        sc_bv lhs,
        const sc_bv& rhs) noexcept {
        return lhs ^= rhs;
    }

    [[nodiscard]] friend sc_bv operator<<(
        sc_bv lhs,
        const std::size_t amount) noexcept {
        return lhs <<= amount;
    }

    [[nodiscard]] friend sc_bv operator>>(
        sc_bv lhs,
        const std::size_t amount) noexcept {
        return lhs >>= amount;
    }

    friend bool operator==(const sc_bv&, const sc_bv&) = default;

private:
    static void check_index(const std::size_t index) {
        if (index >= static_cast<std::size_t>(Width)) {
            throw std::out_of_range{"sc_bv bit index is outside its width"};
        }
    }

    std::bitset<static_cast<std::size_t>(Width)> bits_;
};

template <int Width>
class sc_lv final {
    static_assert(Width > 0, "sc_lv width must be positive");

public:
    sc_lv() { values_.fill(sc_logic{}); }
    explicit sc_lv(const char* text) { assign(text); }
    explicit sc_lv(const std::string_view text) { assign(text); }
    constexpr sc_lv(const std::uint64_t value) noexcept {
        for (int bit = 0; bit < Width; ++bit) {
            values_[static_cast<std::size_t>(bit)] =
                bit < 64
                    && ((value >> static_cast<unsigned>(bit)) & 1U) != 0;
        }
    }

    template <int OtherWidth>
    explicit sc_lv(const sc_bv<OtherWidth>& value) noexcept {
        for (int bit = 0; bit < Width; ++bit) {
            values_[static_cast<std::size_t>(bit)] =
                bit < OtherWidth
                    && value[static_cast<std::size_t>(bit)];
        }
    }

    void assign(const std::string_view text) {
        if (text.size() != static_cast<std::size_t>(Width)) {
            throw std::invalid_argument{"sc_lv text has the wrong width"};
        }
        for (int i = 0; i < Width; ++i) {
            values_[static_cast<std::size_t>(i)] =
                sc_logic{text[static_cast<std::size_t>(Width - 1 - i)]};
        }
    }

    [[nodiscard]] const sc_logic& operator[](const std::size_t index) const {
        return values_.at(index);
    }
    [[nodiscard]] sc_logic& operator[](const std::size_t index) { return values_.at(index); }

    [[nodiscard]] static constexpr int length() noexcept {
        return Width;
    }

    [[nodiscard]] bool is_01() const noexcept {
        return std::ranges::all_of(
            values_,
            [](const sc_logic value) {
                return value.is_01();
            });
    }

    [[nodiscard]] std::uint64_t to_uint64() const {
        std::uint64_t result = 0;
        constexpr auto limit = Width < 64 ? Width : 64;
        for (int bit = 0; bit < Width; ++bit) {
            const auto value =
                values_[static_cast<std::size_t>(bit)];
            if (!value.is_01()) {
                throw std::logic_error{
                    "X or Z cannot be converted to uint64"};
            }
            if (bit < limit && value.to_bool()) {
                result |= std::uint64_t{1}
                    << static_cast<unsigned>(bit);
            }
        }
        return result;
    }

    [[nodiscard]] std::string to_string() const {
        std::string result(static_cast<std::size_t>(Width), 'X');
        for (int i = 0; i < Width; ++i) {
            result[static_cast<std::size_t>(Width - 1 - i)] =
                values_[static_cast<std::size_t>(i)].to_char();
        }
        return result;
    }

    sc_lv& operator&=(const sc_lv& rhs) noexcept {
        for (int bit = 0; bit < Width; ++bit) {
            values_[static_cast<std::size_t>(bit)]
                &= rhs.values_[static_cast<std::size_t>(bit)];
        }
        return *this;
    }

    sc_lv& operator|=(const sc_lv& rhs) noexcept {
        for (int bit = 0; bit < Width; ++bit) {
            values_[static_cast<std::size_t>(bit)]
                |= rhs.values_[static_cast<std::size_t>(bit)];
        }
        return *this;
    }

    sc_lv& operator^=(const sc_lv& rhs) noexcept {
        for (int bit = 0; bit < Width; ++bit) {
            values_[static_cast<std::size_t>(bit)]
                ^= rhs.values_[static_cast<std::size_t>(bit)];
        }
        return *this;
    }

    sc_lv& operator<<=(const std::size_t amount) noexcept {
        const auto original = values_;
        for (int bit = 0; bit < Width; ++bit) {
            const auto index = static_cast<std::size_t>(bit);
            values_[index] =
                amount <= index
                    ? original[index - amount]
                    : sc_logic{false};
        }
        return *this;
    }

    sc_lv& operator>>=(const std::size_t amount) noexcept {
        const auto original = values_;
        for (int bit = 0; bit < Width; ++bit) {
            const auto index = static_cast<std::size_t>(bit);
            values_[index] =
                amount < static_cast<std::size_t>(Width)
                    && index
                            < static_cast<std::size_t>(Width)
                                - amount
                    ? original[index + amount]
                    : sc_logic{false};
        }
        return *this;
    }

    [[nodiscard]] sc_logic and_reduce() const noexcept {
        auto result = sc_logic{true};
        for (const auto value : values_) {
            result &= value;
        }
        return result;
    }

    [[nodiscard]] sc_logic or_reduce() const noexcept {
        auto result = sc_logic{false};
        for (const auto value : values_) {
            result |= value;
        }
        return result;
    }

    [[nodiscard]] sc_logic xor_reduce() const noexcept {
        auto result = sc_logic{false};
        for (const auto value : values_) {
            result ^= value;
        }
        return result;
    }

    [[nodiscard]] friend sc_lv operator~(sc_lv value) noexcept {
        for (auto& bit : value.values_) {
            bit = ~bit;
        }
        return value;
    }

    [[nodiscard]] friend sc_lv operator&(
        sc_lv lhs,
        const sc_lv& rhs) noexcept {
        return lhs &= rhs;
    }

    [[nodiscard]] friend sc_lv operator|(
        sc_lv lhs,
        const sc_lv& rhs) noexcept {
        return lhs |= rhs;
    }

    [[nodiscard]] friend sc_lv operator^(
        sc_lv lhs,
        const sc_lv& rhs) noexcept {
        return lhs ^= rhs;
    }

    [[nodiscard]] friend sc_lv operator<<(
        sc_lv lhs,
        const std::size_t amount) noexcept {
        return lhs <<= amount;
    }

    [[nodiscard]] friend sc_lv operator>>(
        sc_lv lhs,
        const std::size_t amount) noexcept {
        return lhs >>= amount;
    }

    friend bool operator==(const sc_lv&, const sc_lv&) = default;

private:
    std::array<sc_logic, static_cast<std::size_t>(Width)> values_;
};

template <int Width>
class sc_uint final {
    static_assert(Width > 0 && Width <= 64, "sc_uint supports widths 1 through 64");

public:
    class bit_reference final {
    public:
        constexpr bit_reference(
            sc_uint& owner,
            const std::size_t index) noexcept
            : owner_(&owner), index_(index) {}

        constexpr bit_reference& operator=(const bool value) noexcept {
            const auto selected =
                std::uint64_t{1} << static_cast<unsigned>(index_);
            owner_->value_ =
                value
                    ? owner_->value_ | selected
                    : owner_->value_ & ~selected;
            owner_->value_ &= mask();
            return *this;
        }

        constexpr bit_reference& operator=(
            const bit_reference& value) noexcept {
            return *this = static_cast<bool>(value);
        }

        [[nodiscard]] constexpr operator bool() const noexcept {
            return ((owner_->value_
                     >> static_cast<unsigned>(index_))
                    & 1U)
                != 0;
        }

        constexpr void flip() noexcept {
            owner_->value_ ^=
                std::uint64_t{1}
                << static_cast<unsigned>(index_);
        }

    private:
        sc_uint* owner_;
        std::size_t index_;
    };

    constexpr sc_uint() noexcept = default;
    constexpr sc_uint(const std::uint64_t value) noexcept : value_(value & mask()) {}

    template <int OtherWidth>
    explicit sc_uint(const sc_bv<OtherWidth>& value) noexcept
        : value_(value.to_uint64() & mask()) {}

    template <int OtherWidth>
    explicit sc_uint(const sc_lv<OtherWidth>& value)
        : value_(value.to_uint64() & mask()) {}

    constexpr sc_uint& operator=(const std::uint64_t value) noexcept {
        value_ = value & mask();
        return *this;
    }

    [[nodiscard]] static constexpr int length() noexcept {
        return Width;
    }

    [[nodiscard]] constexpr std::uint64_t to_uint64() const noexcept { return value_; }
    [[nodiscard]] constexpr std::uint64_t raw_bits() const noexcept {
        return value_;
    }
    [[nodiscard]] constexpr unsigned int to_uint() const noexcept {
        return static_cast<unsigned int>(value_);
    }
    [[nodiscard]] std::string to_string() const {
        return sc_bv<Width>{value_}.to_string();
    }

    [[nodiscard]] constexpr bool operator[](
        const std::size_t index) const {
        check_index(index);
        return ((value_ >> static_cast<unsigned>(index)) & 1U) != 0;
    }

    [[nodiscard]] constexpr bit_reference operator[](
        const std::size_t index) {
        check_index(index);
        return bit_reference{*this, index};
    }

    constexpr operator std::uint64_t() const noexcept { return value_; }

    constexpr sc_uint& operator+=(const sc_uint rhs) noexcept {
        value_ = (value_ + rhs.value_) & mask();
        return *this;
    }

    constexpr sc_uint& operator-=(const sc_uint rhs) noexcept {
        value_ = (value_ - rhs.value_) & mask();
        return *this;
    }

    constexpr sc_uint& operator*=(const sc_uint rhs) noexcept {
        value_ = (value_ * rhs.value_) & mask();
        return *this;
    }

    constexpr sc_uint& operator/=(const sc_uint rhs) {
        if (rhs.value_ == 0) {
            throw std::domain_error{"sc_uint division by zero"};
        }
        value_ /= rhs.value_;
        return *this;
    }

    constexpr sc_uint& operator%=(const sc_uint rhs) {
        if (rhs.value_ == 0) {
            throw std::domain_error{"sc_uint modulo by zero"};
        }
        value_ %= rhs.value_;
        return *this;
    }

    constexpr sc_uint& operator&=(const sc_uint rhs) noexcept {
        value_ &= rhs.value_;
        return *this;
    }

    constexpr sc_uint& operator|=(const sc_uint rhs) noexcept {
        value_ = (value_ | rhs.value_) & mask();
        return *this;
    }

    constexpr sc_uint& operator^=(const sc_uint rhs) noexcept {
        value_ = (value_ ^ rhs.value_) & mask();
        return *this;
    }

    constexpr sc_uint& operator<<=(const std::size_t amount) noexcept {
        value_ =
            amount >= 64
                ? 0
                : (value_ << static_cast<unsigned>(amount)) & mask();
        return *this;
    }

    constexpr sc_uint& operator>>=(const std::size_t amount) noexcept {
        value_ =
            amount >= 64
                ? 0
                : value_ >> static_cast<unsigned>(amount);
        return *this;
    }

    constexpr sc_uint& operator++() noexcept {
        return *this += sc_uint{1};
    }

    constexpr sc_uint operator++(int) noexcept {
        const auto original = *this;
        ++*this;
        return original;
    }

    constexpr sc_uint& operator--() noexcept {
        return *this -= sc_uint{1};
    }

    constexpr sc_uint operator--(int) noexcept {
        const auto original = *this;
        --*this;
        return original;
    }

    [[nodiscard]] constexpr bool and_reduce() const noexcept {
        return value_ == mask();
    }

    [[nodiscard]] constexpr bool or_reduce() const noexcept {
        return value_ != 0;
    }

    [[nodiscard]] constexpr bool xor_reduce() const noexcept {
        return std::popcount(value_) % 2U != 0;
    }

    [[nodiscard]] friend constexpr sc_uint operator+(
        sc_uint lhs,
        const sc_uint rhs) noexcept {
        return lhs += rhs;
    }

    [[nodiscard]] friend constexpr sc_uint operator-(
        sc_uint lhs,
        const sc_uint rhs) noexcept {
        return lhs -= rhs;
    }

    [[nodiscard]] friend constexpr sc_uint operator*(
        sc_uint lhs,
        const sc_uint rhs) noexcept {
        return lhs *= rhs;
    }

    [[nodiscard]] friend constexpr sc_uint operator/(
        sc_uint lhs,
        const sc_uint rhs) {
        return lhs /= rhs;
    }

    [[nodiscard]] friend constexpr sc_uint operator%(
        sc_uint lhs,
        const sc_uint rhs) {
        return lhs %= rhs;
    }

    [[nodiscard]] friend constexpr sc_uint operator~(
        sc_uint value) noexcept {
        value.value_ = ~value.value_ & mask();
        return value;
    }

    [[nodiscard]] friend constexpr sc_uint operator&(
        sc_uint lhs,
        const sc_uint rhs) noexcept {
        return lhs &= rhs;
    }

    [[nodiscard]] friend constexpr sc_uint operator|(
        sc_uint lhs,
        const sc_uint rhs) noexcept {
        return lhs |= rhs;
    }

    [[nodiscard]] friend constexpr sc_uint operator^(
        sc_uint lhs,
        const sc_uint rhs) noexcept {
        return lhs ^= rhs;
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_uint operator+(
        sc_uint lhs,
        const Integer rhs) noexcept {
        return lhs += from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_uint operator+(
        const Integer lhs,
        sc_uint rhs) noexcept {
        return rhs += from_integer(lhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_uint operator-(
        sc_uint lhs,
        const Integer rhs) noexcept {
        return lhs -= from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_uint operator-(
        const Integer lhs,
        const sc_uint rhs) noexcept {
        return from_integer(lhs) - rhs;
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_uint operator*(
        sc_uint lhs,
        const Integer rhs) noexcept {
        return lhs *= from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_uint operator*(
        const Integer lhs,
        sc_uint rhs) noexcept {
        return rhs *= from_integer(lhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_uint operator/(
        sc_uint lhs,
        const Integer rhs) {
        return lhs /= from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_uint operator/(
        const Integer lhs,
        const sc_uint rhs) {
        return from_integer(lhs) / rhs;
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_uint operator%(
        sc_uint lhs,
        const Integer rhs) {
        return lhs %= from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_uint operator%(
        const Integer lhs,
        const sc_uint rhs) {
        return from_integer(lhs) % rhs;
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_uint operator&(
        sc_uint lhs,
        const Integer rhs) noexcept {
        return lhs &= from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_uint operator&(
        const Integer lhs,
        sc_uint rhs) noexcept {
        return rhs &= from_integer(lhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_uint operator|(
        sc_uint lhs,
        const Integer rhs) noexcept {
        return lhs |= from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_uint operator|(
        const Integer lhs,
        sc_uint rhs) noexcept {
        return rhs |= from_integer(lhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_uint operator^(
        sc_uint lhs,
        const Integer rhs) noexcept {
        return lhs ^= from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_uint operator^(
        const Integer lhs,
        sc_uint rhs) noexcept {
        return rhs ^= from_integer(lhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_uint operator<<(
        sc_uint lhs,
        const Integer amount) {
        return lhs <<= shift_amount(amount);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_uint operator>>(
        sc_uint lhs,
        const Integer amount) {
        return lhs >>= shift_amount(amount);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr bool operator==(
        const sc_uint lhs,
        const Integer rhs) noexcept {
        return lhs == from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr bool operator==(
        const Integer lhs,
        const sc_uint rhs) noexcept {
        return from_integer(lhs) == rhs;
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(
        const sc_uint lhs,
        const Integer rhs) noexcept {
        return lhs <=> from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(
        const Integer lhs,
        const sc_uint rhs) noexcept {
        return from_integer(lhs) <=> rhs;
    }

    friend constexpr auto operator<=>(const sc_uint&, const sc_uint&) noexcept = default;

private:
    static consteval std::uint64_t mask() {
        if constexpr (Width == 64) {
            return std::numeric_limits<std::uint64_t>::max();
        } else {
            return (std::uint64_t{1} << Width) - 1;
        }
    }

    static constexpr void check_index(const std::size_t index) {
        if (index >= static_cast<std::size_t>(Width)) {
            throw std::out_of_range{
                "sc_uint bit index is outside its width"};
        }
    }

    template <typename Integer>
    [[nodiscard]] static constexpr sc_uint from_integer(
        const Integer value) noexcept {
        return sc_uint{static_cast<std::uint64_t>(value)};
    }

    template <typename Integer>
    [[nodiscard]] static constexpr std::size_t shift_amount(
        const Integer amount) {
        if constexpr (std::is_signed_v<Integer>) {
            if (amount < 0) {
                throw std::invalid_argument{
                    "sc_uint shift amount must be nonnegative"};
            }
        }
        const auto converted =
            static_cast<std::uint64_t>(amount);
        return converted
                > static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max())
            ? std::numeric_limits<std::size_t>::max()
            : static_cast<std::size_t>(converted);
    }

    std::uint64_t value_{};
};

template <int Width>
class sc_int final {
    static_assert(Width > 0 && Width <= 64, "sc_int supports widths 1 through 64");

public:
    class bit_reference final {
    public:
        constexpr bit_reference(
            sc_int& owner,
            const std::size_t index) noexcept
            : owner_(&owner), index_(index) {}

        constexpr bit_reference& operator=(const bool value) noexcept {
            const auto selected =
                std::uint64_t{1} << static_cast<unsigned>(index_);
            owner_->bits_ =
                value
                    ? owner_->bits_ | selected
                    : owner_->bits_ & ~selected;
            owner_->bits_ &= mask();
            return *this;
        }

        constexpr bit_reference& operator=(
            const bit_reference& value) noexcept {
            return *this = static_cast<bool>(value);
        }

        [[nodiscard]] constexpr operator bool() const noexcept {
            return ((owner_->bits_
                     >> static_cast<unsigned>(index_))
                    & 1U)
                != 0;
        }

        constexpr void flip() noexcept {
            owner_->bits_ ^=
                std::uint64_t{1}
                << static_cast<unsigned>(index_);
        }

    private:
        sc_int* owner_;
        std::size_t index_;
    };

    constexpr sc_int() noexcept = default;
    constexpr sc_int(const std::int64_t value) noexcept
        : bits_(static_cast<std::uint64_t>(value) & mask()) {}

    template <int OtherWidth>
    explicit sc_int(const sc_bv<OtherWidth>& value) noexcept
        : bits_(value.to_uint64() & mask()) {}

    template <int OtherWidth>
    explicit sc_int(const sc_lv<OtherWidth>& value)
        : bits_(value.to_uint64() & mask()) {}

    constexpr sc_int& operator=(const std::int64_t value) noexcept {
        bits_ = static_cast<std::uint64_t>(value) & mask();
        return *this;
    }

    [[nodiscard]] static constexpr int length() noexcept {
        return Width;
    }

    [[nodiscard]] constexpr std::uint64_t raw_bits() const noexcept {
        return bits_;
    }

    [[nodiscard]] constexpr std::uint64_t to_uint64() const noexcept {
        return bits_;
    }

    [[nodiscard]] constexpr std::int64_t to_int64() const noexcept {
        if constexpr (Width == 64) {
            return std::bit_cast<std::int64_t>(bits_);
        } else {
            const auto sign = std::uint64_t{1} << (Width - 1);
            const auto extended =
                (bits_ & sign) != 0
                    ? bits_ | ~mask()
                    : bits_;
            return std::bit_cast<std::int64_t>(extended);
        }
    }

    [[nodiscard]] std::string to_string() const {
        return sc_bv<Width>{bits_}.to_string();
    }

    [[nodiscard]] constexpr bool operator[](
        const std::size_t index) const {
        check_index(index);
        return ((bits_ >> static_cast<unsigned>(index)) & 1U) != 0;
    }

    [[nodiscard]] constexpr bit_reference operator[](
        const std::size_t index) {
        check_index(index);
        return bit_reference{*this, index};
    }

    constexpr operator std::int64_t() const noexcept {
        return to_int64();
    }

    constexpr sc_int& operator+=(const sc_int rhs) noexcept {
        bits_ = (bits_ + rhs.bits_) & mask();
        return *this;
    }

    constexpr sc_int& operator-=(const sc_int rhs) noexcept {
        bits_ = (bits_ - rhs.bits_) & mask();
        return *this;
    }

    constexpr sc_int& operator*=(const sc_int rhs) noexcept {
        bits_ = (bits_ * rhs.bits_) & mask();
        return *this;
    }

    constexpr sc_int& operator/=(const sc_int rhs) {
        const auto divisor = rhs.to_int64();
        if (divisor == 0) {
            throw std::domain_error{"sc_int division by zero"};
        }
        const auto dividend = to_int64();
        if constexpr (Width == 64) {
            if (dividend == std::numeric_limits<std::int64_t>::min()
                && divisor == -1) {
                return *this;
            }
        }
        bits_ =
            static_cast<std::uint64_t>(dividend / divisor)
            & mask();
        return *this;
    }

    constexpr sc_int& operator%=(const sc_int rhs) {
        const auto divisor = rhs.to_int64();
        if (divisor == 0) {
            throw std::domain_error{"sc_int modulo by zero"};
        }
        const auto dividend = to_int64();
        if constexpr (Width == 64) {
            if (dividend == std::numeric_limits<std::int64_t>::min()
                && divisor == -1) {
                bits_ = 0;
                return *this;
            }
        }
        bits_ =
            static_cast<std::uint64_t>(dividend % divisor)
            & mask();
        return *this;
    }

    constexpr sc_int& operator&=(const sc_int rhs) noexcept {
        bits_ &= rhs.bits_;
        return *this;
    }

    constexpr sc_int& operator|=(const sc_int rhs) noexcept {
        bits_ = (bits_ | rhs.bits_) & mask();
        return *this;
    }

    constexpr sc_int& operator^=(const sc_int rhs) noexcept {
        bits_ = (bits_ ^ rhs.bits_) & mask();
        return *this;
    }

    constexpr sc_int& operator<<=(const std::size_t amount) noexcept {
        bits_ =
            amount >= 64
                ? 0
                : (bits_ << static_cast<unsigned>(amount)) & mask();
        return *this;
    }

    constexpr sc_int& operator>>=(const std::size_t amount) noexcept {
        const bool negative =
            (bits_ & (std::uint64_t{1} << (Width - 1))) != 0;
        if (amount >= static_cast<std::size_t>(Width)) {
            bits_ = negative ? mask() : 0;
            return *this;
        }
        if (amount == 0) {
            return *this;
        }
        bits_ >>= static_cast<unsigned>(amount);
        if (negative) {
            const auto retained =
                (std::uint64_t{1}
                 << (Width - static_cast<int>(amount)))
                - 1U;
            bits_ |= mask() & ~retained;
        }
        return *this;
    }

    constexpr sc_int& operator++() noexcept {
        return *this += sc_int{1};
    }

    constexpr sc_int operator++(int) noexcept {
        const auto original = *this;
        ++*this;
        return original;
    }

    constexpr sc_int& operator--() noexcept {
        return *this -= sc_int{1};
    }

    constexpr sc_int operator--(int) noexcept {
        const auto original = *this;
        --*this;
        return original;
    }

    [[nodiscard]] constexpr bool and_reduce() const noexcept {
        return bits_ == mask();
    }

    [[nodiscard]] constexpr bool or_reduce() const noexcept {
        return bits_ != 0;
    }

    [[nodiscard]] constexpr bool xor_reduce() const noexcept {
        return std::popcount(bits_) % 2U != 0;
    }

    [[nodiscard]] friend constexpr sc_int operator+(
        sc_int lhs,
        const sc_int rhs) noexcept {
        return lhs += rhs;
    }

    [[nodiscard]] friend constexpr sc_int operator-(
        sc_int lhs,
        const sc_int rhs) noexcept {
        return lhs -= rhs;
    }

    [[nodiscard]] friend constexpr sc_int operator*(
        sc_int lhs,
        const sc_int rhs) noexcept {
        return lhs *= rhs;
    }

    [[nodiscard]] friend constexpr sc_int operator/(
        sc_int lhs,
        const sc_int rhs) {
        return lhs /= rhs;
    }

    [[nodiscard]] friend constexpr sc_int operator%(
        sc_int lhs,
        const sc_int rhs) {
        return lhs %= rhs;
    }

    [[nodiscard]] friend constexpr sc_int operator-(
        sc_int value) noexcept {
        value.bits_ = (~value.bits_ + 1U) & mask();
        return value;
    }

    [[nodiscard]] friend constexpr sc_int operator~(
        sc_int value) noexcept {
        value.bits_ = ~value.bits_ & mask();
        return value;
    }

    [[nodiscard]] friend constexpr sc_int operator&(
        sc_int lhs,
        const sc_int rhs) noexcept {
        return lhs &= rhs;
    }

    [[nodiscard]] friend constexpr sc_int operator|(
        sc_int lhs,
        const sc_int rhs) noexcept {
        return lhs |= rhs;
    }

    [[nodiscard]] friend constexpr sc_int operator^(
        sc_int lhs,
        const sc_int rhs) noexcept {
        return lhs ^= rhs;
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_int operator+(
        sc_int lhs,
        const Integer rhs) noexcept {
        return lhs += from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_int operator+(
        const Integer lhs,
        sc_int rhs) noexcept {
        return rhs += from_integer(lhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_int operator-(
        sc_int lhs,
        const Integer rhs) noexcept {
        return lhs -= from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_int operator-(
        const Integer lhs,
        const sc_int rhs) noexcept {
        return from_integer(lhs) - rhs;
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_int operator*(
        sc_int lhs,
        const Integer rhs) noexcept {
        return lhs *= from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_int operator*(
        const Integer lhs,
        sc_int rhs) noexcept {
        return rhs *= from_integer(lhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_int operator/(
        sc_int lhs,
        const Integer rhs) {
        return lhs /= from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_int operator/(
        const Integer lhs,
        const sc_int rhs) {
        return from_integer(lhs) / rhs;
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_int operator%(
        sc_int lhs,
        const Integer rhs) {
        return lhs %= from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_int operator%(
        const Integer lhs,
        const sc_int rhs) {
        return from_integer(lhs) % rhs;
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_int operator&(
        sc_int lhs,
        const Integer rhs) noexcept {
        return lhs &= from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_int operator&(
        const Integer lhs,
        sc_int rhs) noexcept {
        return rhs &= from_integer(lhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_int operator|(
        sc_int lhs,
        const Integer rhs) noexcept {
        return lhs |= from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_int operator|(
        const Integer lhs,
        sc_int rhs) noexcept {
        return rhs |= from_integer(lhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_int operator^(
        sc_int lhs,
        const Integer rhs) noexcept {
        return lhs ^= from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_int operator^(
        const Integer lhs,
        sc_int rhs) noexcept {
        return rhs ^= from_integer(lhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_int operator<<(
        sc_int lhs,
        const Integer amount) {
        return lhs <<= shift_amount(amount);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr sc_int operator>>(
        sc_int lhs,
        const Integer amount) {
        return lhs >>= shift_amount(amount);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr bool operator==(
        const sc_int lhs,
        const Integer rhs) noexcept {
        return lhs == from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr bool operator==(
        const Integer lhs,
        const sc_int rhs) noexcept {
        return from_integer(lhs) == rhs;
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(
        const sc_int lhs,
        const Integer rhs) noexcept {
        return lhs <=> from_integer(rhs);
    }

    template <typename Integer>
        requires std::is_integral_v<Integer>
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(
        const Integer lhs,
        const sc_int rhs) noexcept {
        return from_integer(lhs) <=> rhs;
    }

    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(
        const sc_int lhs,
        const sc_int rhs) noexcept {
        return lhs.to_int64() <=> rhs.to_int64();
    }

    [[nodiscard]] friend constexpr bool operator==(
        const sc_int lhs,
        const sc_int rhs) noexcept {
        return lhs.bits_ == rhs.bits_;
    }

private:
    static consteval std::uint64_t mask() {
        if constexpr (Width == 64) {
            return std::numeric_limits<std::uint64_t>::max();
        } else {
            return (std::uint64_t{1} << Width) - 1;
        }
    }

    static constexpr void check_index(const std::size_t index) {
        if (index >= static_cast<std::size_t>(Width)) {
            throw std::out_of_range{
                "sc_int bit index is outside its width"};
        }
    }

    template <typename Integer>
    [[nodiscard]] static constexpr sc_int from_integer(
        const Integer value) noexcept {
        sc_int result;
        result.bits_ =
            static_cast<std::uint64_t>(value) & mask();
        return result;
    }

    template <typename Integer>
    [[nodiscard]] static constexpr std::size_t shift_amount(
        const Integer amount) {
        if constexpr (std::is_signed_v<Integer>) {
            if (amount < 0) {
                throw std::invalid_argument{
                    "sc_int shift amount must be nonnegative"};
            }
        }
        const auto converted =
            static_cast<std::uint64_t>(amount);
        return converted
                > static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max())
            ? std::numeric_limits<std::size_t>::max()
            : static_cast<std::size_t>(converted);
    }

    std::uint64_t bits_{};
};

} // namespace sc_dt

