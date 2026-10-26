// SPDX-License-Identifier: Apache-2.0
#include "fsim/support/payload_checksum.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace fsim::support {
namespace {

constexpr std::uint64_t prime1 = 0x9E3779B185EBCA87ULL;
constexpr std::uint64_t prime2 = 0xC2B2AE3D27D4EB4FULL;
constexpr std::uint64_t prime3 = 0x165667B19E3779F9ULL;
constexpr std::uint64_t prime4 = 0x85EBCA77C2B2AE63ULL;
constexpr std::uint64_t prime5 = 0x27D4EB2F165667C5ULL;

constexpr std::string_view xxh64_tag = "xxh64:";

// Little-endian reads (compilers fold the byte loop into one load).
std::uint64_t read64(const std::byte* bytes) noexcept
{
    std::uint64_t value {};
    for (std::size_t index = 0U; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[index]))
            << (8U * index);
    }
    return value;
}

std::uint32_t read32(const std::byte* bytes) noexcept
{
    std::uint32_t value {};
    for (std::size_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[index]))
            << (8U * index);
    }
    return value;
}

std::uint64_t round(std::uint64_t lane, const std::uint64_t input) noexcept
{
    lane += input * prime2;
    lane = std::rotl(lane, 31);
    return lane * prime1;
}

std::uint64_t merge_round(std::uint64_t hash, const std::uint64_t lane) noexcept
{
    hash ^= round(0U, lane);
    return hash * prime1 + prime4;
}

void consume_stripes(std::array<std::uint64_t, 4>& lanes,
    const std::byte* bytes, const std::size_t stripes) noexcept
{
    auto v1 = lanes[0];
    auto v2 = lanes[1];
    auto v3 = lanes[2];
    auto v4 = lanes[3];
    for (std::size_t stripe = 0U; stripe < stripes; ++stripe, bytes += 32) {
        v1 = round(v1, read64(bytes));
        v2 = round(v2, read64(bytes + 8));
        v3 = round(v3, read64(bytes + 16));
        v4 = round(v4, read64(bytes + 24));
    }
    lanes = { v1, v2, v3, v4 };
}

std::string hex64(const std::uint64_t value)
{
    constexpr std::string_view digits = "0123456789abcdef";
    std::string text(16U, '0');
    for (std::size_t nibble = 0U; nibble < 16U; ++nibble) {
        text[nibble] = digits[(value >> ((15U - nibble) * 4U)) & 0xfU];
    }
    return text;
}

bool lower_hex(const std::string_view text) noexcept
{
    return std::ranges::all_of(text, [](const unsigned char character) {
        return (character >= '0' && character <= '9')
            || (character >= 'a' && character <= 'f');
    });
}

} // namespace

Xxh64::Xxh64() noexcept
    : lanes_ { prime1 + prime2, prime2, 0U, 0U - prime1 }
{
}

void Xxh64::update(const std::span<const std::byte> bytes) noexcept
{
    const auto* data = bytes.data();
    auto size = bytes.size();
    total_bytes_ += size;
    if (buffered_ != 0U) {
        const auto take = std::min(size, buffer_.size() - buffered_);
        std::memcpy(buffer_.data() + buffered_, data, take);
        buffered_ += take;
        data += take;
        size -= take;
        if (buffered_ < buffer_.size()) {
            return;
        }
        consume_stripes(lanes_, buffer_.data(), 1U);
        buffered_ = 0U;
    }
    const auto stripes = size / 32U;
    consume_stripes(lanes_, data, stripes);
    data += stripes * 32U;
    size -= stripes * 32U;
    std::memcpy(buffer_.data(), data, size);
    buffered_ = size;
}

void Xxh64::update(const std::string_view text) noexcept
{
    update(std::as_bytes(std::span { text.data(), text.size() }));
}

std::uint64_t Xxh64::finish() const noexcept
{
    std::uint64_t hash = total_bytes_ >= 32U
        ? std::rotl(lanes_[0], 1) + std::rotl(lanes_[1], 7)
            + std::rotl(lanes_[2], 12) + std::rotl(lanes_[3], 18)
        : lanes_[2] + prime5;
    if (total_bytes_ >= 32U) {
        for (const auto lane : lanes_) {
            hash = merge_round(hash, lane);
        }
    }
    hash += total_bytes_;
    const auto* tail = buffer_.data();
    auto remaining = buffered_;
    for (; remaining >= 8U; remaining -= 8U, tail += 8) {
        hash ^= round(0U, read64(tail));
        hash = std::rotl(hash, 27) * prime1 + prime4;
    }
    if (remaining >= 4U) {
        hash ^= static_cast<std::uint64_t>(read32(tail)) * prime1;
        hash = std::rotl(hash, 23) * prime2 + prime3;
        remaining -= 4U;
        tail += 4;
    }
    for (; remaining > 0U; --remaining, ++tail) {
        hash ^= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(*tail)) * prime5;
        hash = std::rotl(hash, 11) * prime1;
    }
    hash ^= hash >> 33;
    hash *= prime2;
    hash ^= hash >> 29;
    hash *= prime3;
    hash ^= hash >> 32;
    return hash;
}

std::uint64_t Xxh64::digest(const std::string_view text) noexcept
{
    Xxh64 hasher;
    hasher.update(text);
    return hasher.finish();
}

std::string payload_checksum(const std::string_view bytes)
{
    return std::string { xxh64_tag } + hex64(Xxh64::digest(bytes));
}

bool payload_checksum_spelling(const std::string_view checksum) noexcept
{
    if (checksum.starts_with(xxh64_tag)) {
        const auto digits = checksum.substr(xxh64_tag.size());
        return digits.size() == 16U && lower_hex(digits);
    }
    return checksum.size() == 64U && lower_hex(checksum);
}

PayloadChecksumVerifier::PayloadChecksumVerifier(const std::string_view expected)
    : expected_ { expected }
    , xxh64_ { expected.starts_with(xxh64_tag) }
{
}

void PayloadChecksumVerifier::update(const std::string_view bytes) noexcept
{
    if (xxh64_) {
        xxh64_state_.update(bytes);
    } else {
        sha256_state_.update(bytes);
    }
}

bool PayloadChecksumVerifier::matches()
{
    return xxh64_
        ? std::string { xxh64_tag } + hex64(xxh64_state_.finish()) == expected_
        : Sha256::hex(sha256_state_.finish()) == expected_;
}

bool payload_checksum_matches(
    const std::string_view bytes, const std::string_view expected)
{
    PayloadChecksumVerifier verifier { expected };
    verifier.update(bytes);
    return verifier.matches();
}

} // namespace fsim::support
