// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace fsim::support {

// A bounded sink for the two byte containers used by persistent codecs.
// Callers retain ownership of framing, diagnostics, and the output buffer.
template <typename Buffer>
class BoundedByteWriter final {
    static_assert(std::is_same_v<Buffer, std::string>
        || std::is_same_v<Buffer, std::vector<std::byte>>);

public:
    explicit BoundedByteWriter(Buffer& buffer, const std::size_t maximum_bytes)
        : buffer_(buffer), maximum_bytes_(maximum_bytes)
    {
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return buffer_.size() <= maximum_bytes_;
    }

    [[nodiscard]] bool append(const std::span<const std::byte> bytes)
    {
        if (!valid() || bytes.size() > maximum_bytes_ - buffer_.size()) {
            return false;
        }
        if (bytes.empty()) {
            return true;
        }
        std::vector<std::byte> staged_bytes;
        auto source = bytes;
        if (overlaps_buffer(bytes)) {
            staged_bytes.assign(bytes.begin(), bytes.end());
            source = std::span<const std::byte> {
                staged_bytes.data(), staged_bytes.size() };
        }
        const auto offset = buffer_.size();
        buffer_.resize(offset + source.size());
        std::memcpy(buffer_.data() + offset, source.data(), source.size());
        return true;
    }

    [[nodiscard]] bool append(const std::string_view text)
    {
        return append(std::as_bytes(std::span { text.data(), text.size() }));
    }

    [[nodiscard]] bool write_u8(const std::uint8_t value)
    {
        const std::array bytes { static_cast<std::byte>(value) };
        return append(bytes);
    }

    [[nodiscard]] bool write_u16_le(const std::uint16_t value)
    {
        return write_le(value);
    }

    [[nodiscard]] bool write_u32_le(const std::uint32_t value)
    {
        return write_le(value);
    }

    [[nodiscard]] bool write_u64_le(const std::uint64_t value)
    {
        return write_le(value);
    }

    [[nodiscard]] bool write_u16_be(const std::uint16_t value)
    {
        return write_be(value);
    }

    [[nodiscard]] bool write_u32_be(const std::uint32_t value)
    {
        return write_be(value);
    }

    [[nodiscard]] bool write_u64_be(const std::uint64_t value)
    {
        return write_be(value);
    }

private:
    [[nodiscard]] bool overlaps_buffer(
        const std::span<const std::byte> bytes) const noexcept
    {
        if (buffer_.empty()) {
            return false;
        }
        const auto* const buffer_begin
            = reinterpret_cast<const std::byte*>(buffer_.data());
        const auto* const buffer_end = buffer_begin + buffer_.size();
        const auto* const bytes_end = bytes.data() + bytes.size();
        const std::less<const std::byte*> before;
        return before(buffer_begin, bytes_end)
            && before(bytes.data(), buffer_end);
    }

    template <typename Unsigned>
    [[nodiscard]] bool write_le(const Unsigned value)
    {
        static_assert(std::is_unsigned_v<Unsigned>);
        std::array<std::byte, sizeof(Unsigned)> bytes { };
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::byte>(
                (value >> (index * 8U)) & Unsigned { 0xffU });
        }
        return append(bytes);
    }

    template <typename Unsigned>
    [[nodiscard]] bool write_be(const Unsigned value)
    {
        static_assert(std::is_unsigned_v<Unsigned>);
        std::array<std::byte, sizeof(Unsigned)> bytes { };
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            bytes[bytes.size() - index - 1U] = static_cast<std::byte>(
                (value >> (index * 8U)) & Unsigned { 0xffU });
        }
        return append(bytes);
    }

    Buffer& buffer_;
    std::size_t maximum_bytes_;
};

class BoundedByteReader final {
public:
    explicit BoundedByteReader(const std::span<const std::byte> bytes,
        const std::size_t maximum_bytes = std::numeric_limits<std::size_t>::max())
        : bytes_(bytes), maximum_bytes_(maximum_bytes)
    {
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return bytes_.size() <= maximum_bytes_;
    }

    [[nodiscard]] std::size_t position() const noexcept { return offset_; }
    [[nodiscard]] std::size_t remaining() const noexcept
    {
        return valid() ? bytes_.size() - offset_ : 0U;
    }
    [[nodiscard]] bool finished() const noexcept
    {
        return valid() && offset_ == bytes_.size();
    }

    [[nodiscard]] bool take(const std::size_t count,
        std::span<const std::byte>& output) noexcept
    {
        if (!valid() || count > remaining()) {
            return false;
        }
        output = bytes_.subspan(offset_, count);
        offset_ += count;
        return true;
    }

    [[nodiscard]] bool read_u8(std::uint8_t& output) noexcept
    {
        return read_le(output);
    }

    [[nodiscard]] bool read_u16_le(std::uint16_t& output) noexcept
    {
        return read_le(output);
    }

    [[nodiscard]] bool read_u32_le(std::uint32_t& output) noexcept
    {
        return read_le(output);
    }

    [[nodiscard]] bool read_u64_le(std::uint64_t& output) noexcept
    {
        return read_le(output);
    }

    [[nodiscard]] bool read_u16_be(std::uint16_t& output) noexcept
    {
        return read_be(output);
    }

    [[nodiscard]] bool read_u32_be(std::uint32_t& output) noexcept
    {
        return read_be(output);
    }

    [[nodiscard]] bool read_u64_be(std::uint64_t& output) noexcept
    {
        return read_be(output);
    }

private:
    template <typename Unsigned>
    [[nodiscard]] bool read_le(Unsigned& output) noexcept
    {
        static_assert(std::is_unsigned_v<Unsigned>);
        if (!valid() || sizeof(Unsigned) > remaining()) {
            return false;
        }
        Unsigned value { };
        for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
            value |= static_cast<Unsigned>(
                std::to_integer<std::uint8_t>(bytes_[offset_ + index]))
                << (index * 8U);
        }
        output = value;
        offset_ += sizeof(Unsigned);
        return true;
    }

    template <typename Unsigned>
    [[nodiscard]] bool read_be(Unsigned& output) noexcept
    {
        static_assert(std::is_unsigned_v<Unsigned>);
        if (!valid() || sizeof(Unsigned) > remaining()) {
            return false;
        }
        Unsigned value { };
        for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
            value = static_cast<Unsigned>((value << 8U)
                | std::to_integer<std::uint8_t>(bytes_[offset_ + index]));
        }
        output = value;
        offset_ += sizeof(Unsigned);
        return true;
    }

    std::span<const std::byte> bytes_;
    std::size_t maximum_bytes_;
    std::size_t offset_ { };
};

} // namespace fsim::support
