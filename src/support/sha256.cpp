// SPDX-License-Identifier: Apache-2.0
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

#if (defined(__x86_64__) || defined(_M_X64)) \
    && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define FSIM_SHA256_X86_INTRINSICS 1
#endif

namespace fsim::support {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

constexpr std::uint32_t choose(
    const std::uint32_t x, const std::uint32_t y, const std::uint32_t z) noexcept {
    return (x & y) ^ (~x & z);
}

constexpr std::uint32_t majority(
    const std::uint32_t x, const std::uint32_t y, const std::uint32_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
}

constexpr std::uint32_t big_sigma0(const std::uint32_t x) noexcept {
    return std::rotr(x, 2) ^ std::rotr(x, 13) ^ std::rotr(x, 22);
}

constexpr std::uint32_t big_sigma1(const std::uint32_t x) noexcept {
    return std::rotr(x, 6) ^ std::rotr(x, 11) ^ std::rotr(x, 25);
}

constexpr std::uint32_t small_sigma0(const std::uint32_t x) noexcept {
    return std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3U);
}

constexpr std::uint32_t small_sigma1(const std::uint32_t x) noexcept {
    return std::rotr(x, 17) ^ std::rotr(x, 19) ^ (x >> 10U);
}

std::uint32_t load_be(const std::byte* data) noexcept {
    return (std::to_integer<std::uint32_t>(data[0]) << 24U)
        | (std::to_integer<std::uint32_t>(data[1]) << 16U)
        | (std::to_integer<std::uint32_t>(data[2]) << 8U)
        | std::to_integer<std::uint32_t>(data[3]);
}

#if defined(FSIM_SHA256_X86_INTRINSICS)
[[nodiscard]] bool supports_sha256_instructions() noexcept
{
    static const bool available = [] {
        __builtin_cpu_init();
        return __builtin_cpu_supports("sha")
            && __builtin_cpu_supports("ssse3")
            && __builtin_cpu_supports("sse4.1");
    }();
    return available;
}

__attribute__((target("sha,ssse3,sse4.1")))
void transform_sha256_instructions(
    std::uint32_t* const state,
    const std::byte* const block) noexcept
{
    auto temporary = _mm_loadu_si128(
        reinterpret_cast<const __m128i*>(state));
    auto state1 = _mm_loadu_si128(
        reinterpret_cast<const __m128i*>(state + 4U));
    temporary = _mm_shuffle_epi32(temporary, 0xb1);
    state1 = _mm_shuffle_epi32(state1, 0x1b);
    auto state0 = _mm_alignr_epi8(temporary, state1, 8);
    state1 = _mm_blend_epi16(state1, temporary, 0xf0);
    const auto saved0 = state0;
    const auto saved1 = state1;

    const auto byte_shuffle = _mm_set_epi8(
        12, 13, 14, 15,
        8, 9, 10, 11,
        4, 5, 6, 7,
        0, 1, 2, 3);
    __m128i messages[4];
    for (std::size_t index = 0; index < 4U; ++index) {
        messages[index] = _mm_shuffle_epi8(
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(
                block + index * 16U)),
            byte_shuffle);
    }

    for (std::size_t group = 0; group < 16U; ++group) {
        const auto slot = group & 3U;
        if (group >= 4U) {
            auto scheduled = _mm_sha256msg1_epu32(
                messages[slot], messages[(slot + 1U) & 3U]);
            const auto middle = _mm_alignr_epi8(
                messages[(slot + 3U) & 3U],
                messages[(slot + 2U) & 3U],
                4);
            scheduled = _mm_add_epi32(scheduled, middle);
            messages[slot] = _mm_sha256msg2_epu32(
                scheduled, messages[(slot + 3U) & 3U]);
        }
        auto rounds = _mm_add_epi32(
            messages[slot],
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(
                kRoundConstants.data() + group * 4U)));
        state1 = _mm_sha256rnds2_epu32(state1, state0, rounds);
        rounds = _mm_shuffle_epi32(rounds, 0x0e);
        state0 = _mm_sha256rnds2_epu32(state0, state1, rounds);
    }

    state0 = _mm_add_epi32(state0, saved0);
    state1 = _mm_add_epi32(state1, saved1);
    temporary = _mm_shuffle_epi32(state0, 0x1b);
    state1 = _mm_shuffle_epi32(state1, 0xb1);
    state0 = _mm_blend_epi16(temporary, state1, 0xf0);
    state1 = _mm_alignr_epi8(state1, temporary, 8);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(state), state0);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(state + 4U), state1);
}
#endif

} // namespace

Sha256::Sha256() noexcept
    : state_{
          0x6a09e667U,
          0xbb67ae85U,
          0x3c6ef372U,
          0xa54ff53aU,
          0x510e527fU,
          0x9b05688cU,
          0x1f83d9abU,
          0x5be0cd19U,
      } {}

void Sha256::update(const std::span<const std::byte> bytes) noexcept {
    if (finished_ || bytes.empty()) {
        return;
    }

    total_bytes_ += bytes.size();
    std::size_t offset = 0;
    if (buffered_ != 0U) {
        const auto count = std::min(buffer_.size() - buffered_, bytes.size() - offset);
        std::copy_n(bytes.data() + offset, count, buffer_.data() + buffered_);
        buffered_ += count;
        offset += count;
        if (buffered_ == buffer_.size()) {
            transform(buffer_.data());
            buffered_ = 0;
        }
    }
    while (bytes.size() - offset >= buffer_.size()) {
        transform(bytes.data() + offset);
        offset += buffer_.size();
    }
    if (offset != bytes.size()) {
        const auto count = bytes.size() - offset;
        std::copy_n(bytes.data() + offset, count, buffer_.data());
        buffered_ = count;
    }
}

void Sha256::update(const std::string_view text) noexcept {
    update(std::as_bytes(std::span{text.data(), text.size()}));
}

Sha256::Digest Sha256::finish() noexcept {
    if (!finished_) {
        const auto bit_length = total_bytes_ * 8U;
        buffer_[buffered_++] = std::byte{0x80};

        if (buffered_ > 56) {
            std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_), buffer_.end(), std::byte{0});
            transform(buffer_.data());
            buffered_ = 0;
        }

        std::fill(
            buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_),
            buffer_.begin() + 56,
            std::byte{0});
        for (std::size_t i = 0; i < 8; ++i) {
            buffer_[63 - i] = static_cast<std::byte>((bit_length >> (i * 8U)) & 0xffU);
        }
        transform(buffer_.data());
        buffered_ = 0;
        finished_ = true;
    }

    Digest result{};
    for (std::size_t word = 0; word < state_.size(); ++word) {
        for (std::size_t byte = 0; byte < 4; ++byte) {
            result[word * 4 + byte] =
                static_cast<std::uint8_t>((state_[word] >> ((3U - byte) * 8U)) & 0xffU);
        }
    }
    return result;
}

Sha256::Digest Sha256::digest(const std::span<const std::byte> bytes) noexcept {
    Sha256 hasher;
    hasher.update(bytes);
    return hasher.finish();
}

Sha256::Digest Sha256::digest(const std::string_view text) noexcept {
    Sha256 hasher;
    hasher.update(text);
    return hasher.finish();
}

std::string Sha256::hex(const Digest& digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.resize(digest.size() * 2);
    for (std::size_t i = 0; i < digest.size(); ++i) {
        result[i * 2] = digits[digest[i] >> 4U];
        result[i * 2 + 1] = digits[digest[i] & 0x0fU];
    }
    return result;
}

void Sha256::transform(const std::byte* block) noexcept {
#if defined(FSIM_SHA256_X86_INTRINSICS)
    if (supports_sha256_instructions()) {
        transform_sha256_instructions(state_.data(), block);
        return;
    }
#endif
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i) {
        words[i] = load_be(block + i * 4);
    }
    for (std::size_t i = 16; i < words.size(); ++i) {
        words[i] =
            small_sigma1(words[i - 2]) + words[i - 7] + small_sigma0(words[i - 15]) + words[i - 16];
    }

    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];
    auto f = state_[5];
    auto g = state_[6];
    auto h = state_[7];

    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto temp1 = h + big_sigma1(e) + choose(e, f, g) + kRoundConstants[i] + words[i];
        const auto temp2 = big_sigma0(a) + majority(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

} // namespace fsim::support
