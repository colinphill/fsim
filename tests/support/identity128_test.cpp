// SPDX-License-Identifier: Apache-2.0
#include "fsim/support/identity128.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

struct PointId {
    std::uint64_t high { };
    std::uint64_t low { };
};

struct TransactionId {
    std::uint64_t high { };
    std::uint64_t low { };
};

void test_domain_fields_and_hex()
{
    constexpr PointId empty { };
    constexpr PointId high_only { 1U, 0U };
    constexpr TransactionId low_only { 0U, 1U };
    static_assert(!fsim::support::identity128_nonzero(empty));
    static_assert(fsim::support::identity128_nonzero(high_only));
    static_assert(fsim::support::identity128_nonzero(low_only));

    constexpr PointId value {
        UINT64_C(0x0123456789abcdef), UINT64_C(0xfedcba9876543210)
    };
    assert(fsim::support::identity128_hex(value)
        == "0123456789abcdeffedcba9876543210");
    assert(fsim::support::identity128_hex(empty)
        == "00000000000000000000000000000000");
}

void test_digest_word_and_hash_input()
{
    constexpr fsim::support::Sha256::Digest digest {
        0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0aU, 0x0bU, 0x0cU, 0x0dU, 0x0eU, 0x0fU,
        0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U,
        0x18U, 0x19U, 0x1aU, 0x1bU, 0x1cU, 0x1dU, 0x1eU, 0x1fU
    };
    assert(fsim::support::sha256_digest_word_be(digest, 0U)
        == UINT64_C(0x0001020304050607));
    assert(fsim::support::sha256_digest_word_be(digest, 8U)
        == UINT64_C(0x08090a0b0c0d0e0f));
    assert(fsim::support::sha256_digest_word_be(digest, 24U)
        == UINT64_C(0x18191a1b1c1d1e1f));

    constexpr std::array encoded {
        std::byte { 0x01 }, std::byte { 0x23 },
        std::byte { 0x45 }, std::byte { 0x67 },
        std::byte { 0x89 }, std::byte { 0xab },
        std::byte { 0xcd }, std::byte { 0xef }
    };
    fsim::support::Sha256 by_words;
    fsim::support::sha256_update_u64_be(
        by_words, UINT64_C(0x0123456789abcdef));
    const auto by_bytes = fsim::support::Sha256::digest(std::span { encoded });
    assert(by_words.finish() == by_bytes);
}

} // namespace

int main()
{
    test_domain_fields_and_hex();
    test_digest_word_and_hash_input();
}
