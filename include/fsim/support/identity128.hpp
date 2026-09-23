// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/support/sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fsim::support {

template <typename Identity>
[[nodiscard]] constexpr bool identity128_nonzero(
    const Identity& identity) noexcept
{
    return identity.high != 0U || identity.low != 0U;
}

template <typename Identity>
[[nodiscard]] std::string identity128_hex(const Identity& identity)
{
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result(32U, '0');
    const std::array<std::uint64_t, 2U> words { identity.high, identity.low };
    std::size_t output = 0U;
    for (const auto word : words) {
        for (std::size_t nibble = 0U; nibble < 16U; ++nibble) {
            const auto shift = static_cast<unsigned>((15U - nibble) * 4U);
            result[output++] = digits[(word >> shift) & 0xfU];
        }
    }
    return result;
}

// The byte offset must identify eight bytes within the SHA-256 digest.
[[nodiscard]] inline std::uint64_t sha256_digest_word_be(
    const Sha256::Digest& digest, const std::size_t first) noexcept
{
    std::uint64_t value { };
    for (std::size_t index = first; index < first + 8U; ++index) {
        value = (value << 8U) | digest[index];
    }
    return value;
}

inline void sha256_update_u64_be(
    Sha256& hash, const std::uint64_t value) noexcept
{
    std::array<std::byte, 8U> bytes { };
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        const auto shift = static_cast<unsigned>((bytes.size() - index - 1U) * 8U);
        bytes[index] = static_cast<std::byte>((value >> shift) & 0xffU);
    }
    hash.update(bytes);
}

} // namespace fsim::support
