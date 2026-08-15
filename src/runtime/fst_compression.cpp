// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/fst_compression.hpp"

#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace fsim::runtime {
namespace {

    using Bytes = std::vector<std::uint8_t>;

    constexpr FstCompressionProfile hierarchy_profile {
        FstCompressionKind::HierarchyGzipStoreV1,
        FstCompressionProfile::schema_version,
        1U,
        15U,
        65'535U,
        0U,
        0U,
        "fst-gzip-store-v1",
    };

    constexpr FstCompressionProfile initial_value_profile {
        FstCompressionKind::InitialValueZlibFixedV1,
        FstCompressionProfile::schema_version,
        1U,
        15U,
        65'535U,
        258U,
        128U,
        "fst-zlib-fixed-rle-v1",
    };

    void require_space(const Bytes& output, const std::size_t count,
        const std::size_t maximum)
    {
        if (count > maximum || output.size() > maximum - count) {
            throw std::length_error("FST compressed output exceeds its byte limit");
        }
    }

    void append_u8(Bytes& output, const std::uint8_t value,
        const std::size_t maximum)
    {
        require_space(output, 1U, maximum);
        output.push_back(value);
    }

    void append_be32(Bytes& output, const std::uint32_t value,
        const std::size_t maximum)
    {
        require_space(output, 4U, maximum);
        for (int shift = 24; shift >= 0; shift -= 8) {
            output.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void append_le16(Bytes& output, const std::uint16_t value,
        const std::size_t maximum)
    {
        require_space(output, 2U, maximum);
        output.push_back(static_cast<std::uint8_t>(value));
        output.push_back(static_cast<std::uint8_t>(value >> 8U));
    }

    void append_le32(Bytes& output, const std::uint32_t value,
        const std::size_t maximum)
    {
        require_space(output, 4U, maximum);
        for (unsigned shift = 0; shift < 32U; shift += 8U) {
            output.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }

    [[nodiscard]] std::uint32_t crc32(
        const std::span<const std::uint8_t> input) noexcept
    {
        std::uint32_t result = UINT32_C(0xffffffff);
        for (const auto byte : input) {
            result ^= byte;
            for (unsigned bit = 0; bit < 8U; ++bit) {
                const auto mask = static_cast<std::uint32_t>(
                    -static_cast<std::int32_t>(result & 1U));
                result = (result >> 1U) ^ (UINT32_C(0xedb88320) & mask);
            }
        }
        return ~result;
    }

    [[nodiscard]] std::uint32_t adler32(
        const std::span<const std::uint8_t> input) noexcept
    {
        constexpr std::uint32_t modulus = 65'521U;
        std::uint32_t first = 1U;
        std::uint32_t second = 0U;
        for (const auto byte : input) {
            first = (first + byte) % modulus;
            second = (second + first) % modulus;
        }
        return (second << 16U) | first;
    }

    [[nodiscard]] std::uint32_t reverse_bits(std::uint32_t value,
        const unsigned count) noexcept
    {
        std::uint32_t result = 0U;
        for (unsigned index = 0; index < count; ++index) {
            result = (result << 1U) | (value & 1U);
            value >>= 1U;
        }
        return result;
    }

    class DeflateBitWriter final {
    public:
        DeflateBitWriter(Bytes& output, const std::size_t maximum)
            : output_(output)
            , maximum_(maximum)
        {
        }

        void append(std::uint32_t value, unsigned count)
        {
            while (count != 0U) {
                const auto available = 8U - bit_count_;
                const auto consumed = std::min(count, available);
                const auto mask = (UINT32_C(1) << consumed) - 1U;
                current_ |= static_cast<std::uint8_t>((value & mask) << bit_count_);
                bit_count_ += consumed;
                value >>= consumed;
                count -= consumed;
                if (bit_count_ == 8U) {
                    append_u8(output_, current_, maximum_);
                    current_ = 0U;
                    bit_count_ = 0U;
                }
            }
        }

        void finish()
        {
            if (bit_count_ != 0U) {
                append_u8(output_, current_, maximum_);
                current_ = 0U;
                bit_count_ = 0U;
            }
        }

    private:
        Bytes& output_;
        std::size_t maximum_ { };
        std::uint8_t current_ { };
        unsigned bit_count_ { };
    };

    void append_fixed_symbol(DeflateBitWriter& output,
        const std::uint16_t symbol)
    {
        std::uint32_t code = 0U;
        unsigned bits = 0U;
        if (symbol <= 143U) {
            code = 0x30U + symbol;
            bits = 8U;
        } else if (symbol <= 255U) {
            code = 0x190U + symbol - 144U;
            bits = 9U;
        } else if (symbol <= 279U) {
            code = symbol - 256U;
            bits = 7U;
        } else {
            code = 0xc0U + symbol - 280U;
            bits = 8U;
        }
        output.append(reverse_bits(code, bits), bits);
    }

    void append_fixed_run(DeflateBitWriter& output, const std::size_t length)
    {
        constexpr std::array<std::uint16_t, 29> bases {
            3,
            4,
            5,
            6,
            7,
            8,
            9,
            10,
            11,
            13,
            15,
            17,
            19,
            23,
            27,
            31,
            35,
            43,
            51,
            59,
            67,
            83,
            99,
            115,
            131,
            163,
            195,
            227,
            258,
        };
        constexpr std::array<std::uint8_t, 29> extras {
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            1,
            1,
            1,
            1,
            2,
            2,
            2,
            2,
            3,
            3,
            3,
            3,
            4,
            4,
            4,
            4,
            5,
            5,
            5,
            5,
            0,
        };
        std::size_t index = 0U;
        while (index + 1U < bases.size() && bases[index + 1U] <= length) {
            ++index;
        }
        append_fixed_symbol(output, static_cast<std::uint16_t>(257U + index));
        if (extras[index] != 0U) {
            output.append(static_cast<std::uint32_t>(length - bases[index]),
                extras[index]);
        }
        output.append(0U, 5U);
    }

    [[nodiscard]] Bytes gzip_store(const std::span<const std::uint8_t> input,
        const FstCompressionLimits limits)
    {
        if (input.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error("FST gzip input exceeds its portable size field");
        }
        Bytes result;
        require_space(result, 10U, limits.maximum_output_bytes);
        result = { 0x1f, 0x8b, 0x08, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0xff };
        std::size_t offset = 0U;
        do {
            const auto remaining = input.size() - offset;
            const auto block_size = static_cast<std::uint16_t>(
                std::min<std::size_t>(remaining, hierarchy_profile.block_bytes));
            const auto final = offset + block_size == input.size();
            append_u8(result, final ? 0x01U : 0x00U,
                limits.maximum_output_bytes);
            append_le16(result, block_size, limits.maximum_output_bytes);
            append_le16(result, static_cast<std::uint16_t>(~block_size),
                limits.maximum_output_bytes);
            require_space(result, block_size, limits.maximum_output_bytes);
            result.insert(result.end(), input.begin() + static_cast<std::ptrdiff_t>(offset),
                input.begin() + static_cast<std::ptrdiff_t>(offset + block_size));
            offset += block_size;
        } while (offset < input.size());
        append_le32(result, crc32(input), limits.maximum_output_bytes);
        append_le32(result, static_cast<std::uint32_t>(input.size()),
            limits.maximum_output_bytes);
        return result;
    }

    [[nodiscard]] Bytes zlib_fixed(const std::span<const std::uint8_t> input,
        const FstCompressionLimits limits)
    {
        Bytes result;
        require_space(result, 2U, limits.maximum_output_bytes);
        result = { 0x78U, 0x01U };
        DeflateBitWriter output { result, limits.maximum_output_bytes };
        std::size_t block_begin = 0U;
        do {
            const auto block_end = std::min(
                input.size(), block_begin + initial_value_profile.block_bytes);
            output.append(block_end == input.size() ? 1U : 0U, 1U);
            output.append(1U, 2U);
            for (std::size_t offset = block_begin; offset < block_end;) {
                std::size_t run = 0U;
                if (offset != block_begin && input[offset] == input[offset - 1U]) {
                    run = 1U;
                    while (run < initial_value_profile.maximum_match_bytes && offset + run < block_end && input[offset + run] == input[offset - 1U]) {
                        ++run;
                    }
                }
                if (run >= 3U) {
                    append_fixed_run(output, run);
                    offset += run;
                } else {
                    append_fixed_symbol(output, input[offset]);
                    ++offset;
                }
            }
            append_fixed_symbol(output, 256U);
            block_begin = block_end;
        } while (block_begin < input.size());
        output.finish();
        append_be32(result, adler32(input), limits.maximum_output_bytes);
        return result;
    }

    [[nodiscard]] std::string digest(
        const std::span<const std::uint8_t> bytes)
    {
        return support::Sha256::hex(
            support::Sha256::digest(std::as_bytes(bytes)));
    }

} // namespace

const FstCompressionProfile& fst_compression_profile(
    const FstCompressionKind kind)
{
    switch (kind) {
    case FstCompressionKind::HierarchyGzipStoreV1:
        return hierarchy_profile;
    case FstCompressionKind::InitialValueZlibFixedV1:
        return initial_value_profile;
    }
    throw std::invalid_argument("unknown FST compression profile");
}

FstCompressionResult compress_fst_block(
    const std::span<const std::uint8_t> input,
    const FstCompressionKind kind, const FstCompressionLimits limits)
{
    if (limits.maximum_input_bytes == 0U || limits.maximum_output_bytes == 0U || input.size() > limits.maximum_input_bytes) {
        throw std::length_error("FST compression exceeds its resource limits");
    }
    const auto& profile = fst_compression_profile(kind);
    FstCompressionResult result;
    result.profile_identity = std::string { profile.identity };
    result.semantic_digest = digest(input);
    switch (kind) {
    case FstCompressionKind::HierarchyGzipStoreV1:
        result.bytes = gzip_store(input, limits);
        break;
    case FstCompressionKind::InitialValueZlibFixedV1:
        result.bytes = zlib_fixed(input, limits);
        break;
    }
    result.byte_digest = digest(result.bytes);
    return result;
}

} // namespace fsim::runtime
