// SPDX-License-Identifier: Apache-2.0
#include "fsim/support/bounded_bytes.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr std::array expected {
    std::byte { 0x12 }, std::byte { 0x56 }, std::byte { 0x34 },
    std::byte { 0x78 }, std::byte { 0x9a }, std::byte { 0xbc },
    std::byte { 0xde }, std::byte { 0xef }, std::byte { 0xcd },
    std::byte { 0xab }, std::byte { 0x89 }, std::byte { 0x67 },
    std::byte { 0x45 }, std::byte { 0x23 }, std::byte { 0x01 },
    std::byte { 0x78 }, std::byte { 0x79 }
};

template <typename Buffer>
void exercise_writer()
{
    Buffer bytes;
    fsim::support::BoundedByteWriter writer { bytes, expected.size() };
    assert(writer.valid());
    assert(writer.write_u8(0x12U));
    assert(writer.write_u16_le(0x3456U));
    assert(writer.write_u32_be(0x789abcdeU));
    assert(writer.write_u64_le(UINT64_C(0x0123456789abcdef)));
    assert(writer.append("xy"));
    assert(std::ranges::equal(
        std::as_bytes(std::span { bytes.data(), bytes.size() }), expected));
    const auto saved = bytes;
    assert(!writer.write_u8(0U));
    assert(!writer.write_u64_be(0U));
    assert(bytes == saved);

    fsim::support::BoundedByteWriter invalid { bytes, expected.size() - 1U };
    assert(!invalid.valid());
    assert(!invalid.append(std::string_view { }));
    assert(bytes == saved);
}

template <typename Buffer, typename Value>
void fill_to_capacity(Buffer& bytes, const Value value)
{
    bytes.reserve(bytes.size());
    while (bytes.size() < bytes.capacity()) {
        bytes.push_back(value);
    }
    assert(!bytes.empty() && bytes.size() == bytes.capacity());
}

void exercise_vector_self_append()
{
    std::vector<std::byte> bytes {
        std::byte { 0x12 }, std::byte { 0x34 }, std::byte { 0x56 }
    };
    fill_to_capacity(bytes, std::byte { 0x78 });
    const auto original = bytes;
    const auto old_capacity = bytes.capacity();
    auto expected_bytes = original;
    expected_bytes.insert(expected_bytes.end(), original.begin(), original.end());
    fsim::support::BoundedByteWriter writer { bytes, original.size() * 2U };

    const std::span<const std::byte> source { bytes.data(), bytes.size() };
    assert(writer.append(source));
    assert(bytes == expected_bytes && bytes.capacity() > old_capacity);

    bytes = original;
    fill_to_capacity(bytes, std::byte { 0x78 });
    const auto string_source = std::string_view {
        reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    const auto before_string_append = bytes;
    const auto string_append_capacity = bytes.capacity();
    auto string_expected = before_string_append;
    string_expected.insert(string_expected.end(), before_string_append.begin(),
        before_string_append.end());
    fsim::support::BoundedByteWriter string_writer {
        bytes, before_string_append.size() * 2U };
    assert(string_writer.append(string_source));
    assert(bytes == string_expected && bytes.capacity() > string_append_capacity);
}

void exercise_string_self_append()
{
    std::string bytes { "seed" };
    fill_to_capacity(bytes, 'x');
    const auto original = bytes;
    const auto old_capacity = bytes.capacity();
    fsim::support::BoundedByteWriter writer { bytes, original.size() * 2U };
    const std::string_view source { bytes.data(), bytes.size() };
    assert(writer.append(source));
    assert(bytes == original + original && bytes.capacity() > old_capacity);

    bytes = original;
    fill_to_capacity(bytes, 'x');
    const auto before_span_append = bytes;
    const auto span_append_capacity = bytes.capacity();
    const auto source_bytes
        = std::as_bytes(std::span { bytes.data(), bytes.size() });
    fsim::support::BoundedByteWriter span_writer {
        bytes, before_span_append.size() * 2U };
    assert(span_writer.append(source_bytes));
    assert(bytes == before_span_append + before_span_append
        && bytes.capacity() > span_append_capacity);
}

void exercise_reader()
{
    fsim::support::BoundedByteReader reader { std::span { expected } };
    assert(reader.valid() && reader.position() == 0U);
    std::uint8_t byte { };
    std::uint16_t word { };
    std::uint32_t dword { };
    std::uint64_t qword { };
    assert(reader.read_u8(byte) && byte == 0x12U);
    assert(reader.read_u16_le(word) && word == 0x3456U);
    assert(reader.read_u32_be(dword) && dword == 0x789abcdeU);
    assert(reader.read_u64_le(qword)
        && qword == UINT64_C(0x0123456789abcdef));
    std::span<const std::byte> tail;
    assert(reader.take(2U, tail));
    assert(std::ranges::equal(tail, std::as_bytes(std::span { "xy", 2U })));
    assert(reader.finished() && reader.remaining() == 0U);

    dword = 0xfeedU;
    assert(!reader.read_u32_le(dword));
    assert(dword == 0xfeedU && reader.position() == expected.size());
    assert(!reader.take(std::numeric_limits<std::size_t>::max(), tail));
    assert(tail.size() == 2U && reader.finished());

    fsim::support::BoundedByteReader invalid {
        std::span { expected }, expected.size() - 1U };
    assert(!invalid.valid() && invalid.remaining() == 0U);
    assert(!invalid.read_u8(byte) && byte == 0x12U);
    assert(invalid.position() == 0U && !invalid.finished());

    constexpr std::array big_endian {
        std::byte { 0x12 }, std::byte { 0x34 },
        std::byte { 0x56 }, std::byte { 0x78 },
        std::byte { 0x9a }, std::byte { 0xbc },
        std::byte { 0xde }, std::byte { 0xf0 }
    };
    fsim::support::BoundedByteReader big_reader {
        std::span { big_endian } };
    assert(big_reader.read_u64_be(qword)
        && qword == UINT64_C(0x123456789abcdef0));
    assert(big_reader.finished());
}

void exercise_bit_patterns_and_length_accounting()
{
    constexpr std::array encoded {
        std::byte { 0xd6 }, std::byte { 0xff }, std::byte { 0xff },
        std::byte { 0xff }, std::byte { 0xff }, std::byte { 0xff },
        std::byte { 0xff }, std::byte { 0xff },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x80 },
        std::byte { 0x3f }, std::byte { 0xf8 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 }
    };
    std::vector<std::byte> bytes;
    fsim::support::BoundedByteWriter writer { bytes, encoded.size() };
    assert(writer.write_u64_le(std::bit_cast<std::uint64_t>(std::int64_t { -42 })));
    assert(writer.write_u32_le(std::bit_cast<std::uint32_t>(-0.0F)));
    assert(writer.write_u64_be(std::bit_cast<std::uint64_t>(1.5)));
    assert(std::ranges::equal(bytes, encoded));
    assert(!writer.write_u8(0U) && bytes.size() == encoded.size());

    fsim::support::BoundedByteReader reader { std::span { encoded } };
    std::uint64_t integer_bits { };
    std::uint32_t float_bits { };
    std::uint64_t double_bits { };
    assert(reader.read_u64_le(integer_bits) && reader.position() == 8U);
    assert(reader.read_u32_le(float_bits) && reader.remaining() == 8U);
    assert(reader.read_u64_be(double_bits) && reader.finished());
    assert(std::bit_cast<std::int64_t>(integer_bits) == -42);
    assert(std::bit_cast<float>(float_bits) == 0.0F);
    assert(float_bits == UINT32_C(0x80000000));
    assert(std::bit_cast<double>(double_bits) == 1.5);
    std::uint8_t extra = 0x5aU;
    assert(!reader.read_u8(extra)
        && extra == 0x5aU && reader.position() == encoded.size());

    constexpr std::array truncated {
        std::byte { 0x01 }, std::byte { 0x02 }, std::byte { 0x03 }
    };
    fsim::support::BoundedByteReader short_reader { std::span { truncated } };
    std::uint32_t unchanged = 0xfeedU;
    assert(!short_reader.read_u32_le(unchanged));
    assert(unchanged == 0xfeedU
        && short_reader.position() == 0U && short_reader.remaining() == 3U);
    std::vector<std::byte> too_small;
    fsim::support::BoundedByteWriter short_writer {
        too_small, truncated.size() };
    assert(!short_writer.write_u32_le(unchanged) && too_small.empty());
}

void exercise_remaining_byte_orders()
{
    constexpr std::array expected_bytes {
        std::byte { 0x12 }, std::byte { 0x34 },
        std::byte { 0x78 }, std::byte { 0x56 },
        std::byte { 0x34 }, std::byte { 0x12 },
        std::byte { 0x01 }, std::byte { 0x23 },
        std::byte { 0x45 }, std::byte { 0x67 },
        std::byte { 0x89 }, std::byte { 0xab },
        std::byte { 0xcd }, std::byte { 0xef }
    };
    std::vector<std::byte> bytes;
    fsim::support::BoundedByteWriter writer { bytes, expected_bytes.size() };
    assert(writer.write_u16_be(0x1234U));
    assert(writer.write_u32_le(0x12345678U));
    assert(writer.write_u64_be(UINT64_C(0x0123456789abcdef)));
    assert(std::ranges::equal(bytes, expected_bytes));

    fsim::support::BoundedByteReader reader { std::span { bytes } };
    std::uint16_t word { };
    std::uint32_t dword { };
    std::uint64_t qword { };
    assert(reader.read_u16_be(word) && word == 0x1234U);
    assert(reader.read_u32_le(dword) && dword == 0x12345678U);
    assert(reader.read_u64_be(qword)
        && qword == UINT64_C(0x0123456789abcdef));
    assert(reader.finished());
}

} // namespace

int main()
{
    exercise_writer<std::string>();
    exercise_writer<std::vector<std::byte>>();
    exercise_vector_self_append();
    exercise_string_self_append();
    exercise_reader();
    exercise_bit_patterns_and_length_accounting();
    exercise_remaining_byte_orders();
}
