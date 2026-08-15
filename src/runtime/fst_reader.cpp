// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/fst_reader.hpp"

#include "fsim/runtime/fst_compression.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace fsim::runtime {
namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint8_t fst_block_header = 0U;
constexpr std::uint8_t fst_block_geometry = 3U;
constexpr std::uint8_t fst_block_hierarchy = 4U;
constexpr std::uint8_t fst_block_value_changes = 8U;
constexpr std::uint8_t fst_block_skip = 0xffU;
constexpr std::uint8_t fst_scope = 0xfeU;
constexpr std::uint8_t fst_upscope = 0xffU;
constexpr std::uint8_t fst_attribute_begin = 0xfcU;
constexpr std::uint64_t fst_header_section_length = 329U;
constexpr std::array<std::uint8_t, 8> fst_endian_test {
    0x69U, 0x57U, 0x14U, 0x8bU, 0x0aU, 0xbfU, 0x05U, 0x40U
};

class ReadFailure final : public std::runtime_error {
public:
    ReadFailure(std::string code, std::string message, const std::size_t offset)
        : std::runtime_error(std::move(message))
        , code_(std::move(code))
        , offset_(offset)
    {
    }

    [[nodiscard]] const std::string& code() const noexcept { return code_; }
    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }

private:
    std::string code_;
    std::size_t offset_ { };
};

[[noreturn]] void malformed(
    const std::string_view message, const std::size_t offset)
{
    throw ReadFailure { "FSIM-FST-READ-001", std::string { message }, offset };
}

[[noreturn]] void inconsistent(
    const std::string_view message, const std::size_t offset)
{
    throw ReadFailure { "FSIM-FST-READ-002", std::string { message }, offset };
}

[[noreturn]] void exhausted(
    const std::string_view message, const std::size_t offset)
{
    throw ReadFailure { "FSIM-FST-READ-003", std::string { message }, offset };
}

[[noreturn]] void corrupt(
    const std::string_view message, const std::size_t offset)
{
    throw ReadFailure { "FSIM-FST-READ-004", std::string { message }, offset };
}

[[noreturn]] void input_failure(
    const std::string_view message, const std::size_t offset)
{
    throw ReadFailure { "FSIM-FST-READ-005", std::string { message }, offset };
}

class Cursor final {
public:
    explicit Cursor(
        const std::span<const std::uint8_t> bytes,
        const std::size_t base = 0U)
        : bytes_(bytes)
        , base_(base)
    {
    }

    [[nodiscard]] std::size_t offset() const noexcept { return base_ + offset_; }
    [[nodiscard]] std::size_t local_offset() const noexcept { return offset_; }
    [[nodiscard]] std::size_t remaining() const noexcept
    {
        return bytes_.size() - offset_;
    }
    [[nodiscard]] bool empty() const noexcept { return offset_ == bytes_.size(); }

    [[nodiscard]] std::uint8_t u8()
    {
        if (empty()) {
            malformed("FST input is truncated", offset());
        }
        return bytes_[offset_++];
    }

    [[nodiscard]] std::uint16_t le16()
    {
        const auto bytes = raw(2U);
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[0])
            | static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bytes[1]) << 8U));
    }

    [[nodiscard]] std::uint32_t le32()
    {
        const auto bytes = raw(4U);
        std::uint32_t result = 0U;
        for (unsigned shift = 0U; shift < 32U; shift += 8U) {
            result |= static_cast<std::uint32_t>(bytes[shift / 8U]) << shift;
        }
        return result;
    }

    [[nodiscard]] std::uint32_t be32()
    {
        const auto bytes = raw(4U);
        std::uint32_t result = 0U;
        for (const auto byte : bytes) {
            result = (result << 8U) | byte;
        }
        return result;
    }

    [[nodiscard]] std::uint64_t le64()
    {
        const auto bytes = raw(8U);
        std::uint64_t result = 0U;
        for (unsigned shift = 0U; shift < 64U; shift += 8U) {
            result |= static_cast<std::uint64_t>(bytes[shift / 8U]) << shift;
        }
        return result;
    }

    [[nodiscard]] std::uint64_t be64()
    {
        const auto bytes = raw(8U);
        std::uint64_t result = 0U;
        for (const auto byte : bytes) {
            result = (result << 8U) | byte;
        }
        return result;
    }

    [[nodiscard]] std::uint64_t varint()
    {
        const auto start = offset();
        std::uint64_t result = 0U;
        unsigned shift = 0U;
        std::size_t count = 0U;
        while (true) {
            const auto byte = u8();
            ++count;
            if (shift == 63U && (byte & 0xfeU) != 0U) {
                malformed("FST varint overflows", start);
            }
            result |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
            if ((byte & 0x80U) == 0U) {
                if (count != 1U && byte == 0U) {
                    malformed("FST varint is not canonical", start);
                }
                return result;
            }
            if (shift >= 63U) {
                malformed("FST varint is unterminated", start);
            }
            shift += 7U;
        }
    }

    [[nodiscard]] std::span<const std::uint8_t> raw(const std::size_t count)
    {
        if (count > remaining()) {
            malformed("FST input is truncated", offset());
        }
        const auto result = bytes_.subspan(offset_, count);
        offset_ += count;
        return result;
    }

    [[nodiscard]] std::string c_string(const std::size_t limit)
    {
        const auto start = offset_;
        const auto found = std::find(
            bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
            bytes_.end(), 0U);
        if (found == bytes_.end()) {
            malformed("FST text is unterminated", offset());
        }
        const auto count = static_cast<std::size_t>(
            found - (bytes_.begin() + static_cast<std::ptrdiff_t>(offset_)));
        if (count > limit) {
            exhausted("FST text exceeds its byte limit", offset());
        }
        offset_ += count + 1U;
        return std::string {
            reinterpret_cast<const char*>(bytes_.data() + start), count
        };
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t base_ { };
    std::size_t offset_ { };
};

[[nodiscard]] std::size_t checked_size(
    const std::uint64_t value, const std::size_t offset)
{
    if (value > std::numeric_limits<std::size_t>::max()) {
        exhausted("FST size exceeds this host", offset);
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] std::uint32_t crc32(
    const std::span<const std::uint8_t> input) noexcept
{
    std::uint32_t result = UINT32_C(0xffffffff);
    for (const auto byte : input) {
        result ^= byte;
        for (unsigned bit = 0U; bit < 8U; ++bit) {
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

[[nodiscard]] Bytes inflate_stored_gzip(
    const std::span<const std::uint8_t> input,
    const std::size_t expected_size,
    const FstReaderLimits& limits,
    const std::size_t base)
{
    if (expected_size > limits.maximum_hierarchy_bytes) {
        exhausted("FST hierarchy exceeds its byte limit", base);
    }
    Cursor cursor { input, base };
    const std::array<std::uint8_t, 10> header {
        0x1fU, 0x8bU, 0x08U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0xffU
    };
    const auto found_header = cursor.raw(header.size());
    if (!std::equal(found_header.begin(), found_header.end(), header.begin())) {
        corrupt("FST hierarchy GZip header is not canonical", base);
    }
    Bytes result;
    result.reserve(expected_size);
    bool final = false;
    while (!final) {
        const auto block_offset = cursor.offset();
        const auto control = cursor.u8();
        if ((control & 0xfeU) != 0U) {
            corrupt("FST hierarchy uses an unsupported DEFLATE block", block_offset);
        }
        final = (control & 1U) != 0U;
        const auto count = cursor.le16();
        const auto complement = cursor.le16();
        if (static_cast<std::uint16_t>(~count) != complement) {
            corrupt("FST hierarchy block length checksum failed", block_offset);
        }
        if (count > expected_size - result.size()) {
            corrupt("FST hierarchy expands beyond its declared size", block_offset);
        }
        const auto bytes = cursor.raw(count);
        result.insert(result.end(), bytes.begin(), bytes.end());
    }
    if (cursor.remaining() != 8U) {
        corrupt("FST hierarchy GZip stream is trailing or truncated", cursor.offset());
    }
    const auto expected_crc = cursor.le32();
    const auto input_size = cursor.le32();
    if (result.size() != expected_size
        || input_size != static_cast<std::uint32_t>(result.size())
        || expected_crc != crc32(result)) {
        corrupt("FST hierarchy GZip checksum or size failed", cursor.offset());
    }
    return result;
}

class BitCursor final {
public:
    BitCursor(
        const std::span<const std::uint8_t> bytes,
        const std::size_t base)
        : bytes_(bytes)
        , base_(base)
    {
    }

    [[nodiscard]] std::uint32_t bit()
    {
        if (bit_offset_ >= bytes_.size() * 8U) {
            malformed("FST zlib bitstream is truncated", offset());
        }
        const auto result = static_cast<std::uint32_t>(
            (bytes_[bit_offset_ / 8U] >> (bit_offset_ % 8U)) & 1U);
        ++bit_offset_;
        return result;
    }

    [[nodiscard]] std::uint32_t bits(const unsigned count)
    {
        std::uint32_t result = 0U;
        for (unsigned index = 0U; index < count; ++index) {
            result |= bit() << index;
        }
        return result;
    }

    [[nodiscard]] std::uint32_t huffman_bits(const unsigned count)
    {
        std::uint32_t result = 0U;
        for (unsigned index = 0U; index < count; ++index) {
            result = (result << 1U) | bit();
        }
        return result;
    }

    [[nodiscard]] std::size_t offset() const noexcept
    {
        return base_ + bit_offset_ / 8U;
    }

    void require_zero_padding()
    {
        while ((bit_offset_ & 7U) != 0U) {
            if (bit() != 0U) {
                corrupt("FST zlib padding is nonzero", offset());
            }
        }
        if (bit_offset_ != bytes_.size() * 8U) {
            corrupt("FST zlib stream has trailing bytes", offset());
        }
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t base_ { };
    std::size_t bit_offset_ { };
};

[[nodiscard]] std::uint16_t fixed_symbol(BitCursor& bits)
{
    auto code = bits.huffman_bits(7U);
    if (code <= 23U) {
        return static_cast<std::uint16_t>(256U + code);
    }
    code = (code << 1U) | bits.bit();
    if (code >= 0x30U && code <= 0xbfU) {
        return static_cast<std::uint16_t>(code - 0x30U);
    }
    if (code >= 0xc0U && code <= 0xc7U) {
        return static_cast<std::uint16_t>(280U + code - 0xc0U);
    }
    code = (code << 1U) | bits.bit();
    if (code >= 0x190U && code <= 0x1ffU) {
        return static_cast<std::uint16_t>(144U + code - 0x190U);
    }
    corrupt("FST zlib fixed-Huffman symbol is invalid", bits.offset());
}

[[nodiscard]] std::uint32_t reverse_bits(
    std::uint32_t value, const unsigned count) noexcept
{
    std::uint32_t result = 0U;
    for (unsigned index = 0U; index < count; ++index) {
        result = (result << 1U) | (value & 1U);
        value >>= 1U;
    }
    return result;
}

void append_fixed_match(
    BitCursor& bits,
    const std::uint16_t symbol,
    Bytes& output,
    const std::size_t limit)
{
    constexpr std::array<std::uint16_t, 29> length_bases {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
        31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    constexpr std::array<std::uint8_t, 29> length_extras {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
        2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
    };
    constexpr std::array<std::uint16_t, 30> distance_bases {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
        6145, 8193, 12289, 16385, 24577
    };
    constexpr std::array<std::uint8_t, 30> distance_extras {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
        6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };
    if (symbol < 257U || symbol > 285U) {
        corrupt("FST zlib length symbol is reserved", bits.offset());
    }
    const auto length_index = static_cast<std::size_t>(symbol - 257U);
    auto length = static_cast<std::size_t>(length_bases[length_index]);
    if (length_extras[length_index] != 0U) {
        length += bits.bits(length_extras[length_index]);
    }
    const auto distance_code = reverse_bits(bits.bits(5U), 5U);
    if (distance_code >= distance_bases.size()) {
        corrupt("FST zlib distance symbol is reserved", bits.offset());
    }
    auto distance = static_cast<std::size_t>(distance_bases[distance_code]);
    if (distance_extras[distance_code] != 0U) {
        distance += bits.bits(distance_extras[distance_code]);
    }
    if (distance == 0U || distance > output.size()
        || length > limit - output.size()) {
        corrupt("FST zlib match exceeds its history or output limit", bits.offset());
    }
    for (std::size_t index = 0U; index < length; ++index) {
        output.push_back(output[output.size() - distance]);
    }
}

[[nodiscard]] Bytes inflate_fixed_zlib(
    const std::span<const std::uint8_t> input,
    const std::size_t expected_size,
    const std::size_t limit,
    const std::size_t base)
{
    if (input.size() < 8U || expected_size > limit) {
        exhausted("FST zlib input exceeds its reader limit", base);
    }
    if (input[0] != 0x78U || input[1] != 0x01U) {
        corrupt("FST zlib header is not canonical", base);
    }
    const auto deflate = input.subspan(2U, input.size() - 6U);
    BitCursor bits { deflate, base + 2U };
    Bytes result;
    result.reserve(expected_size);
    bool final = false;
    while (!final) {
        final = bits.bit() != 0U;
        if (bits.bits(2U) != 1U) {
            corrupt("FST zlib block is not fixed-Huffman", bits.offset());
        }
        while (true) {
            const auto symbol = fixed_symbol(bits);
            if (symbol < 256U) {
                if (result.size() == limit) {
                    exhausted("FST zlib output exceeds its reader limit", bits.offset());
                }
                result.push_back(static_cast<std::uint8_t>(symbol));
            } else if (symbol == 256U) {
                break;
            } else {
                append_fixed_match(bits, symbol, result, limit);
            }
        }
    }
    bits.require_zero_padding();
    Cursor checksum { input.last(4U), base + input.size() - 4U };
    if (result.size() != expected_size || checksum.be32() != adler32(result)) {
        corrupt("FST zlib checksum or size failed", base + input.size() - 4U);
    }
    return result;
}

struct Header {
    SimulationTick initial_time { };
    SimulationTick final_time { };
    std::uint64_t scope_count { };
    std::uint64_t declaration_count { };
    std::uint64_t handle_count { };
    std::uint64_t value_block_count { };
    std::int8_t timescale_exponent { };
    FstReaderCompression compression { FstReaderCompression::Deterministic };
    std::string profile;
};

[[nodiscard]] Header read_header(Cursor& cursor)
{
    Header result;
    if (cursor.u8() != fst_block_header
        || cursor.be64() != fst_header_section_length) {
        malformed("FST header block is missing or stale", 0U);
    }
    result.initial_time = cursor.be64();
    result.final_time = cursor.be64();
    if (result.final_time < result.initial_time) {
        inconsistent("FST final time precedes initial time", cursor.offset());
    }
    const auto endian = cursor.raw(fst_endian_test.size());
    if (!std::equal(endian.begin(), endian.end(), fst_endian_test.begin())) {
        malformed("FST endian marker is invalid", cursor.offset());
    }
    if (cursor.be64() != 0U) {
        malformed("FST header memory field is unsupported", cursor.offset());
    }
    result.scope_count = cursor.be64();
    result.declaration_count = cursor.be64();
    result.handle_count = cursor.be64();
    result.value_block_count = cursor.be64();
    result.timescale_exponent = static_cast<std::int8_t>(cursor.u8());
    if (result.timescale_exponent > 0 || result.timescale_exponent < -18) {
        inconsistent("FST timescale exponent is unsupported", cursor.offset());
    }
    const auto profile_bytes = cursor.raw(128U);
    const auto terminator = std::find(profile_bytes.begin(), profile_bytes.end(), 0U);
    if (terminator == profile_bytes.end()
        || std::any_of(terminator, profile_bytes.end(), [](const auto byte) {
               return byte != 0U;
           })) {
        malformed("FST profile field is malformed", cursor.offset());
    }
    result.profile.assign(
        reinterpret_cast<const char*>(profile_bytes.data()),
        static_cast<std::size_t>(terminator - profile_bytes.begin()));
    if (result.profile == kFstDeterministicContainerProfile) {
        result.compression = FstReaderCompression::Deterministic;
    } else if (result.profile == kFstStoredContainerProfile) {
        result.compression = FstReaderCompression::None;
    } else {
        malformed("FST profile is stale or unsupported", 74U);
    }
    const auto reserved = cursor.raw(119U);
    if (std::any_of(reserved.begin(), reserved.end(), [](const auto byte) {
            return byte != 0U;
        })
        || cursor.u8() != 0U || cursor.be64() != 0U
        || cursor.local_offset() != 330U) {
        malformed("FST header reserved fields are not canonical", cursor.offset());
    }
    return result;
}

struct Block {
    std::span<const std::uint8_t> payload;
    std::size_t offset { };
};

[[nodiscard]] Block read_block(Cursor& cursor, const std::uint8_t expected_type,
    const FstReaderLimits& limits)
{
    const auto offset = cursor.offset();
    if (cursor.u8() != expected_type) {
        malformed("FST block order or type is invalid", offset);
    }
    const auto length = cursor.be64();
    if (length < 8U) {
        malformed("FST block length is invalid", offset);
    }
    const auto payload_size = checked_size(length - 8U, offset);
    if (payload_size > limits.maximum_block_bytes) {
        exhausted("FST block exceeds its byte limit", offset);
    }
    return { cursor.raw(payload_size), offset + 9U };
}

[[nodiscard]] std::vector<std::size_t> read_geometry(
    const Block block, const Header& header, const FstReaderLimits& limits)
{
    Cursor cursor { block.payload, block.offset };
    const auto uncompressed = checked_size(cursor.be64(), cursor.offset());
    const auto stored = checked_size(cursor.be64(), cursor.offset());
    if (uncompressed != stored || stored != cursor.remaining()) {
        inconsistent("FST geometry lengths disagree", block.offset);
    }
    std::vector<std::size_t> widths;
    while (!cursor.empty()) {
        if (widths.size() == limits.maximum_declarations) {
            exhausted("FST geometry exceeds its handle limit", cursor.offset());
        }
        widths.push_back(checked_size(cursor.varint(), cursor.offset()));
    }
    if (widths.size() != header.handle_count) {
        inconsistent("FST geometry handle count disagrees with its header", block.offset);
    }
    return widths;
}

[[nodiscard]] std::uint64_t parse_unsigned(
    const std::string_view text, const std::size_t offset)
{
    std::uint64_t value { };
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (error != std::errc { } || end != text.data() + text.size()
        || text.empty()) {
        malformed("FST declaration metadata contains an invalid integer", offset);
    }
    return value;
}

class RecordCursor final {
public:
    RecordCursor(const std::string_view text, const std::size_t offset)
        : text_(text)
        , offset_(offset)
    {
    }

    [[nodiscard]] std::string_view field()
    {
        const auto separator = text_.find('|', position_);
        if (separator == std::string_view::npos) {
            malformed("FST declaration metadata field is missing", offset_ + position_);
        }
        const auto result = text_.substr(position_, separator - position_);
        position_ = separator + 1U;
        return result;
    }

    [[nodiscard]] std::string sized(const std::size_t limit)
    {
        const auto colon = text_.find(':', position_);
        if (colon == std::string_view::npos) {
            malformed("FST declaration sized metadata is missing", offset_ + position_);
        }
        const auto count = checked_size(parse_unsigned(
            text_.substr(position_, colon - position_), offset_ + position_),
            offset_ + position_);
        if (count > limit || count > text_.size() - colon - 1U) {
            exhausted("FST declaration metadata exceeds its byte limit", offset_ + position_);
        }
        position_ = colon + 1U;
        const auto result = std::string { text_.substr(position_, count) };
        position_ += count;
        return result;
    }

    [[nodiscard]] bool empty() const noexcept { return position_ == text_.size(); }

private:
    std::string_view text_;
    std::size_t offset_ { };
    std::size_t position_ { };
};

[[nodiscard]] std::size_t storage_width(const FstReaderDeclaration& value)
{
    if (value.type_kind == TraceTypeKind::SystemVerilogString) {
        return 0U;
    }
    if (value.type_kind == TraceTypeKind::SystemVerilogScalar
        && (value.scalar_kind == SystemVerilogScalarKind::ShortReal
            || value.scalar_kind == SystemVerilogScalarKind::Real
            || value.scalar_kind == SystemVerilogScalarKind::Realtime)) {
        return 8U;
    }
    if (value.type_kind == TraceTypeKind::VhdlLogic9) {
        if (value.width > std::numeric_limits<std::size_t>::max() / 4U) {
            exhausted("FST Logic9 width overflows", 0U);
        }
        return value.width * 4U;
    }
    return value.width;
}

[[nodiscard]] std::uint8_t expected_fst_type(
    const FstReaderDeclaration& value)
{
    switch (value.type_kind) {
    case TraceTypeKind::Packed:
    case TraceTypeKind::VhdlLogic9:
    case TraceTypeKind::TypedLeaf:
        return 16U;
    case TraceTypeKind::SystemVerilogString:
        return 21U;
    case TraceTypeKind::Enumeration:
        return 28U;
    case TraceTypeKind::VhdlPhysical:
        return 1U;
    case TraceTypeKind::VhdlTime:
        return 8U;
    case TraceTypeKind::SystemVerilogScalar:
        switch (value.scalar_kind) {
        case SystemVerilogScalarKind::ShortReal:
            return 29U;
        case SystemVerilogScalarKind::Real:
            return 3U;
        case SystemVerilogScalarKind::Realtime:
            return 20U;
        case SystemVerilogScalarKind::Time:
            return 8U;
        case SystemVerilogScalarKind::Chandle:
            return 16U;
        case SystemVerilogScalarKind::None:
            break;
        }
        break;
    }
    inconsistent("FST declaration type is unsupported", 0U);
}

[[nodiscard]] FstReaderDeclaration parse_declaration_record(
    const std::string_view record,
    const FstReaderLimits& limits,
    const std::size_t offset)
{
    constexpr std::string_view prefix = "fsim-trace-declaration-v1|";
    if (!record.starts_with(prefix)) {
        malformed("FST declaration metadata prefix is invalid", offset);
    }
    RecordCursor cursor { record.substr(prefix.size()), offset + prefix.size() };
    FstReaderDeclaration result;
    const auto kind = cursor.field();
    if (kind == "v") {
        result.kind = TraceDeclarationKind::Variable;
    } else if (kind == "a") {
        result.kind = TraceDeclarationKind::Alias;
    } else {
        malformed("FST declaration metadata kind is invalid", offset);
    }
    result.signal = { parse_unsigned(cursor.field(), offset) };
    result.target = { parse_unsigned(cursor.field(), offset) };
    const auto type_kind = parse_unsigned(cursor.field(), offset);
    const auto scalar_kind = parse_unsigned(cursor.field(), offset);
    result.width = checked_size(parse_unsigned(cursor.field(), offset), offset);
    const auto source_kind = parse_unsigned(cursor.field(), offset);
    const auto language = parse_unsigned(cursor.field(), offset);
    if (result.signal.value == 0U
        || type_kind > static_cast<std::uint8_t>(TraceTypeKind::TypedLeaf)
        || scalar_kind > static_cast<std::uint8_t>(SystemVerilogScalarKind::Chandle)
        || source_kind > static_cast<std::uint8_t>(TraceSourceKind::Internal)
        || language > static_cast<std::uint8_t>(TraceLanguage::SystemC)) {
        inconsistent("FST declaration metadata enum or identity is invalid", offset);
    }
    result.type_kind = static_cast<TraceTypeKind>(type_kind);
    result.scalar_kind = static_cast<SystemVerilogScalarKind>(scalar_kind);
    result.source.kind = static_cast<TraceSourceKind>(source_kind);
    result.source.language = static_cast<TraceLanguage>(language);
    result.path = cursor.sized(limits.maximum_text_bytes);
    result.canonical_metadata = cursor.sized(limits.maximum_metadata_bytes);
    result.source.root_identity = cursor.sized(limits.maximum_text_bytes);
    result.source.library = cursor.sized(limits.maximum_text_bytes);
    result.source.owner_identity = cursor.sized(limits.maximum_text_bytes);
    result.source.canonical_name = cursor.sized(limits.maximum_text_bytes);
    if (!cursor.empty() || result.path.empty()
        || (result.kind == TraceDeclarationKind::Variable
            && result.target.value != 0U)
        || (result.kind == TraceDeclarationKind::Alias
            && result.target.value == 0U)
        || (result.type_kind == TraceTypeKind::SystemVerilogString
            ? result.width != 0U
            : result.width == 0U)
        || (result.type_kind == TraceTypeKind::SystemVerilogScalar
            ? result.scalar_kind == SystemVerilogScalarKind::None
            : result.scalar_kind != SystemVerilogScalarKind::None)) {
        inconsistent("FST declaration metadata is inconsistent", offset);
    }
    result.storage_width = storage_width(result);
    return result;
}

struct HierarchyResult {
    std::vector<FstReaderScope> scopes;
    std::vector<FstReaderDeclaration> declarations;
    std::vector<std::size_t> handle_declarations;
};

class HierarchyDecoder final {
public:
    HierarchyDecoder(const std::span<const std::uint8_t> bytes,
        const std::size_t offset, const Header& header,
        const std::vector<std::size_t>& widths,
        const FstReaderLimits& limits)
        : cursor_(bytes, offset)
        , header_(header)
        , widths_(widths)
        , limits_(limits)
        , block_offset_(offset)
    {
    }

    [[nodiscard]] HierarchyResult decode()
    {
        while (!cursor_.empty()) {
            const auto offset = cursor_.offset();
            const auto tag = cursor_.u8();
            if (tag == fst_scope) {
                read_scope(offset);
            } else if (tag == fst_upscope) {
                read_upscope(offset);
            } else if (tag == fst_attribute_begin) {
                read_attribute(offset);
            } else {
                read_declaration(tag, offset);
            }
        }
        validate_complete();
        validate_aliases();
        return std::move(result_);
    }

private:
    void read_scope(const std::size_t offset)
    {
        if (pending_) {
            inconsistent("FST declaration metadata is detached", offset);
        }
        if (result_.scopes.size() == limits_.maximum_scopes) {
            exhausted("FST hierarchy exceeds its scope limit", offset);
        }
        FstReaderScope scope;
        scope.kind = cursor_.u8();
        scope.component = cursor_.c_string(limits_.maximum_text_bytes);
        scope.owner_identity = cursor_.c_string(limits_.maximum_text_bytes);
        if (scope.component.empty()) {
            inconsistent("FST scope name is empty", offset);
        }
        const auto parent_bytes
            = scope_stack_.empty() ? 0U : scope_stack_.back().size();
        const auto separator_bytes = scope_stack_.empty() ? 0U : 1U;
        if (parent_bytes > limits_.maximum_text_bytes
            || separator_bytes > limits_.maximum_text_bytes - parent_bytes
            || scope.component.size()
                > limits_.maximum_text_bytes - parent_bytes - separator_bytes) {
            exhausted("FST scope path exceeds its text limit", offset);
        }
        const auto path_bytes
            = parent_bytes + separator_bytes + scope.component.size();
        if (scope_path_bytes_ > limits_.maximum_hierarchy_bytes
            || path_bytes
                > limits_.maximum_hierarchy_bytes - scope_path_bytes_) {
            exhausted("FST scope paths exceed their aggregate limit", offset);
        }
        scope.path = scope_stack_.empty()
            ? scope.component
            : scope_stack_.back() + "." + scope.component;
        scope_path_bytes_ += scope.path.size();
        scope_stack_.push_back(scope.path);
        result_.scopes.push_back(std::move(scope));
    }

    void read_upscope(const std::size_t offset)
    {
        if (scope_stack_.empty() || pending_) {
            inconsistent("FST hierarchy scope stack is invalid", offset);
        }
        scope_stack_.pop_back();
    }

    void read_attribute(const std::size_t offset)
    {
        if (cursor_.u8() != 0U || cursor_.u8() != 0U) {
            malformed("FST hierarchy attribute is unsupported", offset);
        }
        const auto record_offset = cursor_.offset();
        const auto record
            = cursor_.c_string(limits_.maximum_metadata_bytes);
        if (cursor_.varint() != 0U) {
            malformed("FST hierarchy attribute argument is unsupported", offset);
        }
        if (record.starts_with("fsim-trace-declaration-v1|")) {
            if (pending_) {
                inconsistent("FST declaration metadata is duplicated", offset);
            }
            pending_ = parse_declaration_record(
                record, limits_, record_offset);
        } else if (!record.starts_with("fsim-trace-provenance-v1|")) {
            malformed("FST hierarchy metadata record is unsupported", record_offset);
        }
    }

    [[nodiscard]] bool path_matches(
        const FstReaderDeclaration& declaration) const
    {
        const auto direct = scope_stack_.empty()
            || scope_stack_.back() == "__fsim_root";
        if (direct) {
            return declaration.path == declaration.reference;
        }
        const auto& scope = scope_stack_.back();
        return declaration.path.size()
                == scope.size() + 1U + declaration.reference.size()
            && declaration.path.starts_with(scope)
            && declaration.path[scope.size()] == '.'
            && declaration.path.ends_with(declaration.reference);
    }

    void assign_handle(FstReaderDeclaration& declaration,
        const std::uint64_t alias_handle, const std::size_t offset)
    {
        if (declaration.kind == TraceDeclarationKind::Alias) {
            if (alias_handle == 0U || alias_handle > widths_.size()) {
                inconsistent("FST alias handle is invalid", offset);
            }
            declaration.handle = alias_handle;
            return;
        }
        if (alias_handle != 0U) {
            inconsistent("FST variable unexpectedly aliases a handle", offset);
        }
        declaration.handle = result_.handle_declarations.size() + 1U;
        if (declaration.handle > widths_.size()
            || widths_[declaration.handle - 1U]
                != declaration.storage_width) {
            inconsistent("FST variable disagrees with geometry", offset);
        }
        result_.handle_declarations.push_back(result_.declarations.size());
    }

    void read_declaration(
        const std::uint8_t tag, const std::size_t offset)
    {
        if (!pending_) {
            inconsistent("FST variable lacks declaration metadata", offset);
        }
        if (result_.declarations.size() == limits_.maximum_declarations) {
            exhausted("FST hierarchy exceeds its declaration limit", offset);
        }
        auto declaration = std::move(*pending_);
        pending_.reset();
        declaration.fst_type = tag;
        if (cursor_.u8() != 0U) {
            inconsistent("FST variable direction is not implicit", offset);
        }
        declaration.reference
            = cursor_.c_string(limits_.maximum_text_bytes);
        const auto found_width
            = checked_size(cursor_.varint(), cursor_.offset());
        const auto alias_handle = cursor_.varint();
        if (declaration.reference.empty() || !path_matches(declaration)
            || declaration.storage_width != found_width
            || declaration.fst_type != expected_fst_type(declaration)
            || !signal_ids_.insert(declaration.signal.value).second) {
            inconsistent(
                "FST variable disagrees with its declaration metadata", offset);
        }
        assign_handle(declaration, alias_handle, offset);
        result_.declarations.push_back(std::move(declaration));
    }

    void validate_complete() const
    {
        if (pending_ || !scope_stack_.empty()
            || result_.scopes.size() != header_.scope_count
            || result_.declarations.size() != header_.declaration_count
            || result_.handle_declarations.size() != header_.handle_count) {
            inconsistent(
                "FST hierarchy counts or scope closure disagree", block_offset_);
        }
    }

    void validate_aliases() const
    {
        for (const auto& declaration : result_.declarations) {
            if (declaration.kind != TraceDeclarationKind::Alias) {
                continue;
            }
            const auto& target = result_.declarations[
                result_.handle_declarations[declaration.handle - 1U]];
            if (target.signal != declaration.target
                || target.type_kind != declaration.type_kind
                || target.scalar_kind != declaration.scalar_kind
                || target.width != declaration.width
                || target.canonical_metadata
                    != declaration.canonical_metadata) {
                inconsistent(
                    "FST alias metadata disagrees with its target", block_offset_);
            }
        }
    }

    Cursor cursor_;
    const Header& header_;
    const std::vector<std::size_t>& widths_;
    const FstReaderLimits& limits_;
    std::size_t block_offset_ { };
    HierarchyResult result_;
    std::vector<std::string> scope_stack_;
    std::size_t scope_path_bytes_ { };
    std::optional<FstReaderDeclaration> pending_;
    std::unordered_set<std::uint64_t> signal_ids_;
};

[[nodiscard]] HierarchyResult read_hierarchy(
    const Block block,
    const Header& header,
    const std::vector<std::size_t>& widths,
    const FstReaderLimits& limits)
{
    Cursor outer { block.payload, block.offset };
    const auto expected = checked_size(outer.be64(), outer.offset());
    const auto compressed_offset = outer.offset();
    const auto hierarchy = inflate_stored_gzip(
        outer.raw(outer.remaining()), expected, limits, compressed_offset);
    return HierarchyDecoder {
        hierarchy, block.offset, header, widths, limits
    }
        .decode();
}

[[nodiscard]] std::vector<SimulationTick> read_timestamps(
    const std::span<const std::uint8_t> bytes,
    const std::size_t count,
    const Header& header,
    const FstReaderLimits& limits,
    const std::size_t offset)
{
    if (count > limits.maximum_timestamps) {
        exhausted("FST timestamp count exceeds its limit", offset);
    }
    Cursor cursor { bytes, offset };
    std::vector<SimulationTick> result;
    result.reserve(count);
    SimulationTick previous = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
        const auto delta = cursor.varint();
        if (delta > std::numeric_limits<SimulationTick>::max() - previous) {
            inconsistent("FST timestamp delta overflows", cursor.offset());
        }
        previous += delta;
        if (previous < header.initial_time || previous > header.final_time
            || (!result.empty() && previous <= result.back())) {
            inconsistent("FST timestamps are outside or out of order", cursor.offset());
        }
        result.push_back(previous);
    }
    if (!cursor.empty()) {
        malformed("FST timestamp table has trailing bytes", cursor.offset());
    }
    return result;
}

[[nodiscard]] std::vector<std::optional<std::size_t>> read_positions(
    const std::span<const std::uint8_t> bytes,
    const std::size_t handle_count,
    const std::size_t waves_size,
    const std::size_t offset)
{
    Cursor cursor { bytes, offset };
    std::vector<std::optional<std::size_t>> result;
    result.reserve(handle_count);
    std::size_t previous = 0U;
    while (result.size() < handle_count) {
        const auto code = cursor.varint();
        if ((code & 1U) == 0U) {
            const auto count = checked_size(code >> 1U, cursor.offset());
            if (count == 0U || count > handle_count - result.size()) {
                inconsistent("FST zero-position run is invalid", cursor.offset());
            }
            result.resize(result.size() + count);
        } else {
            const auto delta = checked_size(code >> 1U, cursor.offset());
            if (delta == 0U || delta > waves_size - previous) {
                inconsistent("FST wave position delta is invalid", cursor.offset());
            }
            previous += delta;
            result.push_back(previous - 1U);
        }
    }
    if (!cursor.empty()) {
        malformed("FST wave position table has trailing bytes", cursor.offset());
    }
    return result;
}

[[nodiscard]] std::string unpack_binary(
    Cursor& cursor, const std::size_t width)
{
    const auto bytes = cursor.raw((width + 7U) / 8U);
    std::string result(width, '0');
    for (std::size_t index = 0U; index < width; ++index) {
        if ((bytes[index / 8U] & (1U << (7U - index % 8U))) != 0U) {
            result[index] = '1';
        }
    }
    return result;
}

[[nodiscard]] std::string decode_logic9(const std::string_view bits,
    const std::size_t offset)
{
    constexpr std::string_view symbols = "ux01zwlh-";
    std::string result;
    result.reserve(bits.size() / 4U);
    for (std::size_t index = 0U; index < bits.size(); index += 4U) {
        std::uint8_t value = 0U;
        for (std::size_t bit = 0U; bit < 4U; ++bit) {
            const auto bit_value = static_cast<std::uint8_t>(
                bits[index + bit] == '1' ? 1U : 0U);
            value = static_cast<std::uint8_t>(
                static_cast<std::uint8_t>(value << 1U) | bit_value);
        }
        if (value >= symbols.size()) {
            inconsistent("FST Logic9 ordinal is invalid", offset + index / 8U);
        }
        result.push_back(symbols[value]);
    }
    return result;
}

void account_decoded_bytes(const std::size_t bytes,
    std::size_t& decoded_bytes, const std::size_t limit,
    const std::size_t offset)
{
    if (decoded_bytes > limit || bytes > limit - decoded_bytes) {
        exhausted("FST decoded values exceed their byte limit", offset);
    }
    decoded_bytes += bytes;
}

[[nodiscard]] bool is_real_declaration(
    const FstReaderDeclaration& declaration) noexcept
{
    return declaration.type_kind == TraceTypeKind::SystemVerilogScalar
        && (declaration.scalar_kind == SystemVerilogScalarKind::ShortReal
            || declaration.scalar_kind == SystemVerilogScalarKind::Real
            || declaration.scalar_kind == SystemVerilogScalarKind::Realtime);
}

[[nodiscard]] std::size_t read_scalar_wave(const std::uint64_t code,
    FstReaderValue& result, std::size_t& decoded_bytes,
    const std::size_t decoded_limit, const std::size_t offset)
{
    account_decoded_bytes(1U, decoded_bytes, decoded_limit, offset);
    constexpr std::array<char, 4> symbols { '0', 'x', '1', 'z' };
    result.payload_kind = FstPayloadKind::Symbols;
    result.payload.assign(1U, symbols[code & 3U]);
    return checked_size(code >> 2U, offset);
}

[[nodiscard]] std::size_t read_real_wave(Cursor& cursor,
    const std::uint64_t code, FstReaderValue& result,
    std::size_t& decoded_bytes, const std::size_t decoded_limit,
    const std::size_t offset)
{
    if ((code & 1U) == 0U) {
        inconsistent("FST real wave tag is invalid", offset);
    }
    account_decoded_bytes(8U, decoded_bytes, decoded_limit, offset);
    result.payload_kind = FstPayloadKind::Real;
    result.real_bits = cursor.le64();
    return checked_size(code >> 1U, offset);
}

[[nodiscard]] std::size_t read_string_wave(Cursor& cursor,
    const std::uint64_t code, FstReaderValue& result,
    std::size_t& decoded_bytes, const std::size_t decoded_limit,
    const std::size_t offset)
{
    if ((code & 1U) != 0U) {
        inconsistent("FST string wave tag is invalid", offset);
    }
    const auto size = checked_size(cursor.varint(), cursor.offset());
    account_decoded_bytes(size, decoded_bytes, decoded_limit, offset);
    const auto bytes = cursor.raw(size);
    result.payload_kind = FstPayloadKind::String;
    result.payload.assign(
        reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return checked_size(code >> 1U, offset);
}

[[nodiscard]] std::size_t read_vector_wave(Cursor& cursor,
    const std::uint64_t code, const FstReaderDeclaration& declaration,
    FstReaderValue& result, std::size_t& decoded_bytes,
    const std::size_t decoded_limit, const std::size_t offset)
{
    account_decoded_bytes(declaration.storage_width,
        decoded_bytes, decoded_limit, offset);
    result.payload_kind = FstPayloadKind::Symbols;
    if ((code & 1U) == 0U) {
        result.payload = unpack_binary(cursor, declaration.storage_width);
    } else {
        const auto bytes = cursor.raw(declaration.storage_width);
        result.payload.assign(
            reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!std::all_of(result.payload.begin(), result.payload.end(),
                [](const char symbol) {
                    return symbol == '0' || symbol == '1'
                        || symbol == 'x' || symbol == 'z';
                })) {
            inconsistent("FST vector wave contains an invalid symbol", offset);
        }
    }
    if (declaration.type_kind == TraceTypeKind::VhdlLogic9) {
        result.payload = decode_logic9(result.payload, offset);
    }
    return checked_size(code >> 1U, offset);
}

[[nodiscard]] FstReaderValue read_wave_value(
    Cursor& cursor,
    const FstReaderDeclaration& declaration,
    const std::vector<SimulationTick>& timestamps,
    std::size_t& previous_time,
    const std::size_t sequence,
    const std::size_t value_offset,
    std::size_t& decoded_bytes,
    const std::size_t decoded_limit)
{
    FstReaderValue result;
    result.signal = declaration.signal;
    result.handle = declaration.handle;
    result.handle_sequence = sequence;
    const auto code = cursor.varint();
    std::size_t time_delta { };
    if (declaration.storage_width == 1U
        && declaration.type_kind != TraceTypeKind::SystemVerilogString) {
        time_delta = read_scalar_wave(code, result,
            decoded_bytes, decoded_limit, value_offset);
    } else if (is_real_declaration(declaration)) {
        time_delta = read_real_wave(cursor, code, result,
            decoded_bytes, decoded_limit, value_offset);
    } else if (declaration.type_kind == TraceTypeKind::SystemVerilogString) {
        time_delta = read_string_wave(cursor, code, result,
            decoded_bytes, decoded_limit, value_offset);
    } else {
        time_delta = read_vector_wave(cursor, code, declaration, result,
            decoded_bytes, decoded_limit, value_offset);
    }
    if (time_delta > timestamps.size() - previous_time) {
        inconsistent("FST wave timestamp index is invalid", value_offset);
    }
    previous_time += time_delta;
    if (previous_time >= timestamps.size()) {
        inconsistent("FST wave timestamp index is out of range", value_offset);
    }
    result.time = timestamps[previous_time];
    return result;
}

struct ValueFrame {
    std::size_t memory_required { };
    std::size_t handle_count { };
    std::size_t chain_slots { };
    std::span<const std::uint8_t> suffix;
    std::size_t suffix_offset { };
};

[[nodiscard]] ValueFrame read_value_frame(const Block block,
    const Header& header, const std::vector<std::size_t>& widths,
    const FstReaderLimits& limits)
{
    Cursor cursor { block.payload, block.offset };
    if (cursor.be64() != header.initial_time
        || cursor.be64() != header.final_time) {
        inconsistent("FST value frame times disagree with the header", block.offset);
    }
    const auto memory_required = checked_size(cursor.be64(), cursor.offset());
    const auto initial_size = checked_size(cursor.varint(), cursor.offset());
    const auto stored_size = checked_size(cursor.varint(), cursor.offset());
    const auto handle_count = checked_size(cursor.varint(), cursor.offset());
    if (handle_count != widths.size() || initial_size > limits.maximum_block_bytes
        || stored_size > cursor.remaining()) {
        inconsistent("FST initial-value frame sizes disagree", cursor.offset());
    }
    const auto stored = cursor.raw(stored_size);
    Bytes initial;
    if (header.compression == FstReaderCompression::Deterministic
        && initial_size
            >= fst_compression_profile(
                   FstCompressionKind::InitialValueZlibFixedV1)
                   .minimum_input_bytes) {
        initial = inflate_fixed_zlib(
            stored, initial_size, limits.maximum_block_bytes,
            cursor.offset() - stored_size);
    } else {
        if (stored_size != initial_size) {
            inconsistent("FST stored initial values have inconsistent sizes", cursor.offset());
        }
        initial.assign(stored.begin(), stored.end());
    }
    std::size_t expected_initial = 0U;
    for (const auto width : widths) {
        if (width > limits.maximum_block_bytes - expected_initial) {
            exhausted("FST geometry exceeds its decoded byte limit", block.offset);
        }
        expected_initial += width;
    }
    if (initial.size() != expected_initial) {
        inconsistent("FST initial values disagree with geometry", block.offset);
    }
    const auto chain_slots = checked_size(cursor.varint(), cursor.offset());
    if ((chain_slots != 0U && chain_slots != handle_count)
        || cursor.u8() != static_cast<std::uint8_t>('4')) {
        inconsistent("FST wave-chain header is invalid", cursor.offset());
    }
    return { memory_required, handle_count, chain_slots,
        block.payload.last(cursor.remaining()), cursor.offset() };
}

struct ValueSections {
    std::span<const std::uint8_t> waves;
    std::span<const std::uint8_t> positions;
    std::span<const std::uint8_t> times;
    std::size_t positions_offset { };
    std::size_t times_offset { };
    std::size_t timestamp_count { };
};

[[nodiscard]] ValueSections split_value_sections(const ValueFrame& frame)
{
    const auto suffix = frame.suffix;
    if (suffix.size() < 32U) {
        malformed("FST value frame suffix is truncated", frame.suffix_offset);
    }
    Cursor tail {
        suffix.last(24U), frame.suffix_offset + suffix.size() - 24U
    };
    const auto time_size = checked_size(tail.be64(), tail.offset());
    if (tail.be64() != time_size) {
        inconsistent("FST timestamp sizes disagree", tail.offset());
    }
    const auto timestamp_count = checked_size(tail.be64(), tail.offset());
    if (time_size > suffix.size() - 32U) {
        malformed("FST timestamp table exceeds its frame", frame.suffix_offset);
    }
    const auto position_size_field = suffix.size() - 24U - time_size - 8U;
    Cursor position_field { suffix.subspan(position_size_field, 8U),
        frame.suffix_offset + position_size_field };
    const auto position_size = checked_size(
        position_field.be64(), position_field.offset());
    if (position_size > position_size_field) {
        malformed("FST position table exceeds its frame", frame.suffix_offset);
    }
    const auto position_begin = position_size_field - position_size;
    return { suffix.first(position_begin),
        suffix.subspan(position_begin, position_size),
        suffix.subspan(position_size_field + 8U, time_size),
        frame.suffix_offset + position_begin,
        frame.suffix_offset + position_size_field + 8U,
        timestamp_count };
}

[[nodiscard]] std::vector<std::size_t> chain_ends(
    const std::vector<std::optional<std::size_t>>& positions,
    const std::size_t waves_size)
{
    std::vector<std::size_t> result(positions.size(), waves_size);
    auto next_begin = waves_size;
    for (std::size_t handle = positions.size(); handle-- > 0U;) {
        if (positions[handle]) {
            result[handle] = next_begin;
            next_begin = *positions[handle];
        }
    }
    return result;
}

void validate_wave_memory(const ValueFrame& frame,
    const ValueSections& sections,
    const std::vector<std::optional<std::size_t>>& positions)
{
    const auto active_count = static_cast<std::size_t>(std::count_if(
        positions.begin(), positions.end(),
        [](const auto& value) { return value.has_value(); }));
    if ((active_count == 0U ? 0U : frame.handle_count) != frame.chain_slots
        || sections.waves.size() < active_count
        || frame.memory_required != sections.waves.size() - active_count) {
        inconsistent(
            "FST wave memory accounting is invalid", frame.suffix_offset);
    }
}

void decode_wave_chains(const ValueFrame& frame,
    const ValueSections& sections,
    const std::vector<std::optional<std::size_t>>& positions,
    const HierarchyResult& hierarchy, const FstReaderLimits& limits,
    FstReaderTrace& trace)
{
    const auto ends = chain_ends(positions, sections.waves.size());
    std::size_t decoded_value_bytes = 0U;
    for (std::size_t handle = 0U; handle < frame.handle_count; ++handle) {
        if (!positions[handle]) {
            continue;
        }
        const auto begin = *positions[handle];
        const auto end = ends[handle];
        if (begin >= end) {
            inconsistent("FST wave chains overlap or are empty",
                frame.suffix_offset + begin);
        }
        Cursor chain { sections.waves.subspan(begin, end - begin),
            frame.suffix_offset + begin };
        if (chain.varint() != 0U) {
            inconsistent("FST wave chain lacks its sentinel", chain.offset());
        }
        const auto& declaration = hierarchy.declarations[
            hierarchy.handle_declarations[handle]];
        std::size_t time_index = 0U;
        std::size_t sequence = 0U;
        while (!chain.empty()) {
            if (trace.values.size() == limits.maximum_values) {
                exhausted("FST value count exceeds its reader limit", chain.offset());
            }
            trace.values.push_back(read_wave_value(chain, declaration,
                trace.timestamps, time_index, sequence++, chain.offset(),
                decoded_value_bytes, limits.maximum_decoded_value_bytes));
        }
    }
}

void read_values(
    const Block block,
    const Header& header,
    const std::vector<std::size_t>& widths,
    const HierarchyResult& hierarchy,
    const FstReaderLimits& limits,
    FstReaderTrace& trace)
{
    const auto frame = read_value_frame(block, header, widths, limits);
    const auto sections = split_value_sections(frame);
    trace.timestamps = read_timestamps(sections.times,
        sections.timestamp_count, header, limits, sections.times_offset);
    const auto positions = read_positions(sections.positions,
        frame.handle_count, sections.waves.size(), sections.positions_offset);
    validate_wave_memory(frame, sections, positions);
    decode_wave_chains(
        frame, sections, positions, hierarchy, limits, trace);
}

void add_digest_field(support::Sha256& hash, const std::string_view value)
{
    hash.update(std::to_string(value.size()));
    hash.update(":");
    hash.update(value);
}

[[nodiscard]] std::string semantic_digest(const FstReaderTrace& trace)
{
    support::Sha256 hash;
    hash.update("fsim-fst-reader-semantics-v1");
    add_digest_field(hash, std::to_string(trace.initial_time));
    add_digest_field(hash, std::to_string(trace.final_time));
    add_digest_field(hash, std::to_string(trace.timescale_exponent));
    for (const auto& declaration : trace.declarations) {
        add_digest_field(hash, std::to_string(
            static_cast<std::uint8_t>(declaration.kind)));
        add_digest_field(hash, std::to_string(declaration.signal.value));
        add_digest_field(hash, std::to_string(declaration.target.value));
        add_digest_field(hash, std::to_string(declaration.handle));
        add_digest_field(hash, declaration.path);
        add_digest_field(hash, std::to_string(
            static_cast<std::uint8_t>(declaration.type_kind)));
        add_digest_field(hash, std::to_string(
            static_cast<std::uint8_t>(declaration.scalar_kind)));
        add_digest_field(hash, std::to_string(declaration.width));
        add_digest_field(hash, declaration.canonical_metadata);
        add_digest_field(hash, std::to_string(
            static_cast<std::uint8_t>(declaration.source.kind)));
        add_digest_field(hash, std::to_string(
            static_cast<std::uint8_t>(declaration.source.language)));
        add_digest_field(hash, declaration.source.root_identity);
        add_digest_field(hash, declaration.source.library);
        add_digest_field(hash, declaration.source.owner_identity);
        add_digest_field(hash, declaration.source.canonical_name);
    }
    for (const auto timestamp : trace.timestamps) {
        add_digest_field(hash, std::to_string(timestamp));
    }
    for (const auto& value : trace.values) {
        add_digest_field(hash, std::to_string(value.signal.value));
        add_digest_field(hash, std::to_string(value.time));
        add_digest_field(hash, std::to_string(value.handle_sequence));
        add_digest_field(hash, std::to_string(
            static_cast<std::uint8_t>(value.payload_kind)));
        add_digest_field(hash, value.payload);
        add_digest_field(hash, std::to_string(value.real_bits));
    }
    return support::Sha256::hex(hash.finish());
}

[[nodiscard]] FstReaderTrace read_trace(
    const std::span<const std::uint8_t> bytes,
    const FstReaderLimits& limits)
{
    if (limits.maximum_container_bytes == 0U
        || limits.maximum_block_bytes == 0U
        || limits.maximum_hierarchy_bytes == 0U
        || limits.maximum_scopes == 0U
        || limits.maximum_declarations == 0U
        || limits.maximum_timestamps == 0U
        || limits.maximum_values == 0U
        || limits.maximum_decoded_value_bytes == 0U
        || limits.maximum_text_bytes == 0U
        || limits.maximum_metadata_bytes == 0U) {
        exhausted("FST reader limits must be nonzero", 0U);
    }
    if (bytes.size() > limits.maximum_container_bytes) {
        exhausted("FST container exceeds its reader limit", 0U);
    }
    Cursor cursor { bytes };
    const auto header = read_header(cursor);
    std::optional<Block> values;
    bool skipped = false;
    if (cursor.empty()) {
        malformed("FST container lacks data blocks", cursor.offset());
    }
    if (bytes[cursor.local_offset()] == fst_block_skip) {
        const auto offset = cursor.offset();
        if (cursor.u8() != fst_block_skip) {
            malformed("FST skip block is invalid", offset);
        }
        const auto body = cursor.raw(35U);
        if (std::any_of(body.begin(), body.end(), [](const auto byte) {
                return byte != 0U;
            })) {
            malformed("FST skip block is not canonical", offset);
        }
        skipped = true;
    } else {
        values = read_block(cursor, fst_block_value_changes, limits);
    }
    const auto geometry_block = read_block(cursor, fst_block_geometry, limits);
    const auto hierarchy_block = read_block(cursor, fst_block_hierarchy, limits);
    if (!cursor.empty()) {
        malformed("FST container has trailing blocks or bytes", cursor.offset());
    }
    if (header.scope_count > limits.maximum_scopes
        || header.declaration_count > limits.maximum_declarations
        || header.handle_count > limits.maximum_declarations) {
        exhausted("FST header counts exceed reader limits", 0U);
    }
    if (header.value_block_count != (header.handle_count == 0U ? 0U : 1U)
        || skipped != (header.handle_count == 0U)
        || values.has_value() != (header.handle_count != 0U)) {
        inconsistent("FST header counts exceed limits or disagree", 0U);
    }
    const auto widths = read_geometry(geometry_block, header, limits);
    auto hierarchy = read_hierarchy(
        hierarchy_block, header, widths, limits);
    FstReaderTrace result;
    result.initial_time = header.initial_time;
    result.final_time = header.final_time;
    result.timescale_exponent = header.timescale_exponent;
    result.compression = header.compression;
    result.profile = header.profile;
    result.scopes = std::move(hierarchy.scopes);
    result.declarations = hierarchy.declarations;
    if (values) {
        read_values(*values, header, widths, hierarchy, limits, result);
    }
    result.semantic_digest = semantic_digest(result);
    return result;
}

void append_file_chunk(Bytes& bytes, const std::span<const char> chunk,
    const FstReaderLimits& limits)
{
    if (bytes.size() > limits.maximum_container_bytes
        || chunk.size() > limits.maximum_container_bytes - bytes.size()) {
        exhausted("FST file exceeds its reader limit", bytes.size());
    }
    bytes.insert(bytes.end(), chunk.begin(), chunk.end());
}

[[nodiscard]] Bytes read_file_bytes(const std::filesystem::path& path,
    const FstReaderLimits& limits)
{
    std::ifstream input { path, std::ios::binary };
    if (!input) {
        input_failure("cannot open FST input", 0U);
    }
    Bytes bytes;
    constexpr std::size_t chunk_size = 64U << 10U;
    std::array<char, chunk_size> chunk { };
    while (input) {
        input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto count = static_cast<std::size_t>(input.gcount());
        append_file_chunk(bytes, std::span { chunk }.first(count), limits);
    }
    if (input.bad()) {
        input_failure("cannot read FST input", bytes.size());
    }
    return bytes;
}

} // namespace

FstReaderResult read_fst(
    const std::span<const std::uint8_t> bytes,
    const FstReaderLimits limits)
{
    FstReaderResult result;
    try {
        result.trace = read_trace(bytes, limits);
    } catch (const ReadFailure& error) {
        result.diagnostics.push_back(
            { error.code(), error.what(), error.offset() });
    } catch (const std::bad_alloc&) {
        result.diagnostics.push_back({ "FSIM-FST-READ-003",
            "FST reader allocation failed", 0U });
    } catch (const std::exception& error) {
        result.diagnostics.push_back({ "FSIM-FST-READ-003",
            std::string { "FST reader resource failure: " } + error.what(),
            0U });
    }
    return result;
}

FstReaderResult read_fst(
    const std::string_view bytes,
    const FstReaderLimits limits)
{
    return read_fst(std::span {
        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size() },
        limits);
}

FstReaderResult read_fst_file(
    const std::filesystem::path& path,
    const FstReaderLimits limits)
{
    FstReaderResult result;
    try {
        return read_fst(read_file_bytes(path, limits), limits);
    } catch (const ReadFailure& error) {
        result.diagnostics.push_back(
            { error.code(), error.what(), error.offset() });
    } catch (const std::bad_alloc&) {
        result.diagnostics.push_back({ "FSIM-FST-READ-003",
            "FST file allocation failed", 0U });
    } catch (const std::exception& error) {
        result.diagnostics.push_back({ "FSIM-FST-READ-005",
            std::string { "FST file read failed: " } + error.what(), 0U });
    }
    return result;
}

} // namespace fsim::runtime
