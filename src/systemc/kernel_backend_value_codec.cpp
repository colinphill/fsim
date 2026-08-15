// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_value_codec.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::systemc {
namespace {

    constexpr std::array<std::byte, 4> kMagic { std::byte { 'F' },
        std::byte { 'S' }, std::byte { 'K' }, std::byte { 'V' } };
    constexpr std::size_t kHeaderBytes = 56U;

    bool report_error(diagnostic::Engine& diagnostics,
        const SystemCKernelValueCode code, const std::string_view message)
    {
        diagnostics.error(systemc_kernel_value_diagnostic_code(code),
            std::string { message });
        return false;
    }

    bool valid_limits(const SystemCKernelValueLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (limits.max_width_bits == 0U
            || limits.max_encoded_bytes < kHeaderBytes
            || limits.max_type_name_bytes == 0U
            || limits.max_enum_literals == 0U
            || limits.max_enum_literal_bytes == 0U
            || limits.max_enum_text_bytes == 0U) {
            return report_error(diagnostics, SystemCKernelValueCode::resource,
                "SystemC value-codec limits must all be nonzero and contain the header");
        }
        return true;
    }

    bool known_kind(const SystemCKernelValueKind kind) noexcept
    {
        switch (kind) {
        case SystemCKernelValueKind::bit2:
        case SystemCKernelValueKind::logic4:
        case SystemCKernelValueKind::logic9:
        case SystemCKernelValueKind::enumeration:
        case SystemCKernelValueKind::time:
            return true;
        }
        return false;
    }

    bool known_direction(const SystemCKernelRangeDirection direction) noexcept
    {
        return direction == SystemCKernelRangeDirection::ascending
            || direction == SystemCKernelRangeDirection::descending;
    }

    std::optional<std::uint32_t> range_width(
        const SystemCKernelValueRange& range) noexcept
    {
        if (!known_direction(range.direction)
            || (range.direction == SystemCKernelRangeDirection::ascending
                && range.left > range.right)
            || (range.direction == SystemCKernelRangeDirection::descending
                && range.left < range.right)) {
            return std::nullopt;
        }
        const auto distance = range.direction
                == SystemCKernelRangeDirection::ascending
            ? static_cast<std::uint64_t>(range.right)
                - static_cast<std::uint64_t>(range.left)
            : static_cast<std::uint64_t>(range.left)
                - static_cast<std::uint64_t>(range.right);
        if (distance >= std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(distance + 1U);
    }

    std::size_t word_count(const std::uint32_t width) noexcept
    {
        return (static_cast<std::size_t>(width) + 63U) / 64U;
    }

    std::uint64_t final_mask(const std::uint32_t width) noexcept
    {
        const auto remainder = width % 64U;
        return remainder == 0U
            ? std::numeric_limits<std::uint64_t>::max()
            : (std::uint64_t { 1U } << remainder) - 1U;
    }

    bool valid_logic9_planes(const SystemCKernelValue& value) noexcept
    {
        for (std::size_t word = 0U; word < value.planes.front().size(); ++word) {
            const auto invalid = value.planes[3][word]
                & (value.planes[2][word] | value.planes[1][word]
                    | value.planes[0][word]);
            if (invalid != 0U) {
                return false;
            }
        }
        return true;
    }

    bool valid_enum_metadata(const SystemCKernelValue& value,
        const SystemCKernelValueLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (value.type_name.empty() || value.enum_literals.empty()
            || value.enum_literals.size() > limits.max_enum_literals) {
            return report_error(diagnostics, SystemCKernelValueCode::metadata,
                "SystemC enumeration requires a bounded type name and literal table");
        }
        std::size_t text_bytes { };
        std::set<std::string_view> unique;
        for (const auto& literal : value.enum_literals) {
            if (literal.empty()
                || literal.size() > limits.max_enum_literal_bytes
                || literal.size() > limits.max_enum_text_bytes
                || text_bytes > limits.max_enum_text_bytes - literal.size()) {
                return report_error(diagnostics,
                    SystemCKernelValueCode::resource,
                    "SystemC enumeration literal text exceeds its governed limit");
            }
            text_bytes += literal.size();
            if (!unique.insert(literal).second) {
                return report_error(diagnostics,
                    SystemCKernelValueCode::metadata,
                    "SystemC enumeration literals must be unique and ordered");
            }
        }
        const auto required_width = std::max<std::uint32_t>(1U,
            static_cast<std::uint32_t>(std::bit_width(
                static_cast<std::uint32_t>(
                    value.enum_literals.size() - 1U))));
        if (value.width != required_width
            || value.planes[0][0] >= value.enum_literals.size()) {
            return report_error(diagnostics, SystemCKernelValueCode::metadata,
                "SystemC enumeration width or selected ordinal is noncanonical");
        }
        return true;
    }

    bool checked_add(
        std::size_t& total, const std::size_t amount) noexcept
    {
        if (amount > std::numeric_limits<std::size_t>::max() - total) {
            return false;
        }
        total += amount;
        return true;
    }

    std::optional<std::size_t> encoded_size(const SystemCKernelValue& value)
    {
        std::size_t total = kHeaderBytes;
        if (!checked_add(total, value.type_name.size())) {
            return std::nullopt;
        }
        for (const auto& literal : value.enum_literals) {
            if (!checked_add(total, sizeof(std::uint32_t))
                || !checked_add(total, literal.size())) {
                return std::nullopt;
            }
        }
        const auto words = word_count(value.width);
        const auto planes = systemc_kernel_value_plane_count(value.kind);
        if (words > std::numeric_limits<std::size_t>::max()
                    / sizeof(std::uint64_t)
            || words * sizeof(std::uint64_t)
                > std::numeric_limits<std::size_t>::max() / planes
            || !checked_add(
                total, words * sizeof(std::uint64_t) * planes)) {
            return std::nullopt;
        }
        return total;
    }

    class Writer {
    public:
        explicit Writer(const std::size_t size) { bytes_.reserve(size); }

        void u8(const std::uint8_t value)
        {
            bytes_.push_back(std::byte { value });
        }

        void u32(const std::uint32_t value)
        {
            for (unsigned shift = 0U; shift < 32U; shift += 8U) {
                u8(static_cast<std::uint8_t>(value >> shift));
            }
        }

        void u64(const std::uint64_t value)
        {
            for (unsigned shift = 0U; shift < 64U; shift += 8U) {
                u8(static_cast<std::uint8_t>(value >> shift));
            }
        }

        void raw(const std::span<const std::byte> bytes)
        {
            bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
        }

        void text(const std::string_view value)
        {
            raw(std::as_bytes(std::span { value.data(), value.size() }));
        }

        [[nodiscard]] std::vector<std::byte> take()
        {
            return std::move(bytes_);
        }

    private:
        std::vector<std::byte> bytes_;
    };

    class Reader {
    public:
        explicit Reader(const std::span<const std::byte> bytes)
            : bytes_ { bytes }
        {
        }

        bool u8(std::uint8_t& value)
        {
            if (offset_ == bytes_.size()) {
                return false;
            }
            value = std::to_integer<std::uint8_t>(bytes_[offset_++]);
            return true;
        }

        bool u32(std::uint32_t& value)
        {
            value = 0U;
            for (unsigned shift = 0U; shift < 32U; shift += 8U) {
                std::uint8_t byte { };
                if (!u8(byte)) {
                    return false;
                }
                value |= static_cast<std::uint32_t>(byte) << shift;
            }
            return true;
        }

        bool u64(std::uint64_t& value)
        {
            value = 0U;
            for (unsigned shift = 0U; shift < 64U; shift += 8U) {
                std::uint8_t byte { };
                if (!u8(byte)) {
                    return false;
                }
                value |= static_cast<std::uint64_t>(byte) << shift;
            }
            return true;
        }

        bool raw(const std::span<const std::byte> expected)
        {
            if (expected.size() > remaining()
                || !std::ranges::equal(expected,
                    bytes_.subspan(offset_, expected.size()))) {
                return false;
            }
            offset_ += expected.size();
            return true;
        }

        bool text(const std::size_t size, std::string& value)
        {
            if (size > remaining()) {
                return false;
            }
            value.assign(reinterpret_cast<const char*>(bytes_.data() + offset_),
                size);
            offset_ += size;
            return true;
        }

        [[nodiscard]] std::size_t remaining() const noexcept
        {
            return bytes_.size() - offset_;
        }

    private:
        std::span<const std::byte> bytes_;
        std::size_t offset_ { };
    };

    bool valid_value_shape(const SystemCKernelValue& value,
        const SystemCKernelValueLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (!known_kind(value.kind)
            || !known_direction(value.range.direction)) {
            return report_error(diagnostics,
                SystemCKernelValueCode::metadata,
                "SystemC value has an unsupported kind or range direction");
        }
        if (value.width == 0U || value.width > limits.max_width_bits
            || value.type_name.size() > limits.max_type_name_bytes) {
            return report_error(diagnostics,
                SystemCKernelValueCode::resource,
                "SystemC value width or type name exceeds its governed limit");
        }
        const auto declared_width = range_width(value.range);
        const auto expected_planes = systemc_kernel_value_plane_count(value.kind);
        const auto expected_words = word_count(value.width);
        if (!declared_width || *declared_width != value.width
            || value.planes.size() != expected_planes) {
            return report_error(diagnostics,
                SystemCKernelValueCode::metadata,
                "SystemC value range or plane count is inconsistent");
        }
        if (std::ranges::any_of(value.planes, [&](const auto& plane) {
                return plane.size() != expected_words;
            })) {
            return report_error(diagnostics,
                SystemCKernelValueCode::metadata,
                "SystemC value limb count is inconsistent");
        }
        const auto mask = final_mask(value.width);
        if (std::ranges::any_of(value.planes, [&](const auto& plane) {
                return (plane.back() & ~mask) != 0U;
            })) {
            return report_error(diagnostics,
                SystemCKernelValueCode::metadata,
                "SystemC value has nonzero unused limb padding");
        }
        return true;
    }

    bool valid_enumeration_kind_metadata(const SystemCKernelValue& value,
        const SystemCKernelValueLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (value.kind == SystemCKernelValueKind::enumeration) {
            if (value.is_signed || value.time_unit_fs != 0U) {
                return report_error(diagnostics,
                    SystemCKernelValueCode::metadata,
                    "SystemC enumeration values must be unsigned and unitless");
            }
            if (!valid_enum_metadata(value, limits, diagnostics)) {
                return false;
            }
        } else if (!value.enum_literals.empty()) {
            return report_error(diagnostics,
                SystemCKernelValueCode::metadata,
                "Only SystemC enumeration values may carry an enumeration table");
        }
        return true;
    }

    bool valid_time_kind_metadata(const SystemCKernelValue& value,
        diagnostic::Engine& diagnostics)
    {
        if (value.kind == SystemCKernelValueKind::time) {
            if (value.is_signed || value.time_unit_fs == 0U) {
                return report_error(diagnostics,
                    SystemCKernelValueCode::metadata,
                    "SystemC time values require an unsigned nonzero unit");
            }
        } else if (value.time_unit_fs != 0U) {
            return report_error(diagnostics,
                SystemCKernelValueCode::metadata,
                "Only SystemC time values may carry a time unit");
        }
        return true;
    }

    bool valid_value_kind_metadata(const SystemCKernelValue& value,
        const SystemCKernelValueLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (value.kind == SystemCKernelValueKind::logic9
            && !valid_logic9_planes(value)) {
            return report_error(diagnostics,
                SystemCKernelValueCode::metadata,
                "SystemC nine-state value contains an unsupported state encoding");
        }
        return valid_enumeration_kind_metadata(value, limits, diagnostics)
            && valid_time_kind_metadata(value, diagnostics);
    }

    struct EncodedHeader {
        std::uint8_t plane_count { };
        std::uint32_t type_size { };
        std::uint32_t enum_count { };
        std::uint32_t words { };
    };

    bool read_encoded_header(Reader& reader, EncodedHeader& header,
        SystemCKernelValue& value)
    {
        std::uint32_t version { };
        std::uint8_t kind { };
        std::uint8_t is_signed { };
        std::uint8_t direction { };
        std::uint32_t reserved { };
        std::uint64_t left { };
        std::uint64_t right { };
        if (!reader.raw(kMagic) || !reader.u32(version) || !reader.u8(kind)
            || !reader.u8(is_signed) || !reader.u8(direction)
            || !reader.u8(header.plane_count) || !reader.u32(value.width)
            || !reader.u64(left) || !reader.u64(right)
            || !reader.u64(value.time_unit_fs)
            || !reader.u32(header.type_size)
            || !reader.u32(header.enum_count) || !reader.u32(header.words)
            || !reader.u32(reserved)) {
            return false;
        }
        if (version != kSystemCKernelValueCodecVersion || is_signed > 1U
            || reserved != 0U) {
            return false;
        }
        value.kind = static_cast<SystemCKernelValueKind>(kind);
        value.is_signed = is_signed != 0U;
        value.range = { static_cast<std::int64_t>(left),
            static_cast<std::int64_t>(right),
            static_cast<SystemCKernelRangeDirection>(direction) };
        return true;
    }

    bool valid_encoded_header(const EncodedHeader& header,
        const SystemCKernelValue& value,
        const SystemCKernelValueLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (!known_kind(value.kind)
            || !known_direction(value.range.direction)) {
            return report_error(diagnostics,
                SystemCKernelValueCode::payload,
                "SystemC value header declares an unsupported kind or range direction");
        }
        if (value.width == 0U || value.width > limits.max_width_bits
            || header.type_size > limits.max_type_name_bytes
            || header.enum_count > limits.max_enum_literals) {
            return report_error(diagnostics,
                SystemCKernelValueCode::resource,
                "SystemC value header declares excessive dimensions");
        }
        if (header.plane_count
                != systemc_kernel_value_plane_count(value.kind)
            || header.words != word_count(value.width)) {
            return report_error(diagnostics,
                SystemCKernelValueCode::payload,
                "SystemC value header declares noncanonical plane dimensions");
        }
        return true;
    }

    bool read_enum_literals(Reader& reader, const std::uint32_t count,
        SystemCKernelValue& value, const SystemCKernelValueLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        std::size_t text_bytes { };
        value.enum_literals.reserve(count);
        for (std::uint32_t index = 0U; index < count; ++index) {
            std::uint32_t literal_size { };
            std::string literal;
            if (!reader.u32(literal_size)
                || literal_size > limits.max_enum_literal_bytes
                || literal_size > limits.max_enum_text_bytes
                || text_bytes > limits.max_enum_text_bytes - literal_size
                || !reader.text(literal_size, literal)) {
                return report_error(diagnostics,
                    SystemCKernelValueCode::resource,
                    "SystemC value enumeration text is truncated or excessive");
            }
            text_bytes += literal_size;
            value.enum_literals.push_back(std::move(literal));
        }
        return true;
    }

    bool read_planes(Reader& reader, const EncodedHeader& header,
        SystemCKernelValue& value, diagnostic::Engine& diagnostics)
    {
        value.planes.assign(header.plane_count,
            std::vector<std::uint64_t>(header.words));
        for (auto& plane : value.planes) {
            for (auto& word : plane) {
                if (!reader.u64(word)) {
                    return report_error(diagnostics,
                        SystemCKernelValueCode::payload,
                        "SystemC value limb planes are truncated");
                }
            }
        }
        return true;
    }

} // namespace

std::size_t systemc_kernel_value_plane_count(
    const SystemCKernelValueKind kind) noexcept
{
    switch (kind) {
    case SystemCKernelValueKind::bit2:
    case SystemCKernelValueKind::enumeration:
    case SystemCKernelValueKind::time:
        return 1U;
    case SystemCKernelValueKind::logic4:
        return 2U;
    case SystemCKernelValueKind::logic9:
        return 4U;
    }
    return 0U;
}

bool validate_systemc_kernel_value(const SystemCKernelValue& value,
    const SystemCKernelValueLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)
        || !valid_value_shape(value, limits, diagnostics)
        || !valid_value_kind_metadata(value, limits, diagnostics)) {
        return false;
    }
    const auto size = encoded_size(value);
    if (!size || *size > limits.max_encoded_bytes) {
        return report_error(diagnostics, SystemCKernelValueCode::resource,
            "SystemC value encoding exceeds its governed byte limit");
    }
    return true;
}

std::optional<std::vector<std::byte>> serialize_systemc_kernel_value(
    const SystemCKernelValue& value, const SystemCKernelValueLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!validate_systemc_kernel_value(value, limits, diagnostics)) {
        return std::nullopt;
    }
    const auto size = *encoded_size(value);
    Writer writer { size };
    writer.raw(kMagic);
    writer.u32(kSystemCKernelValueCodecVersion);
    writer.u8(static_cast<std::uint8_t>(value.kind));
    writer.u8(value.is_signed ? 1U : 0U);
    writer.u8(static_cast<std::uint8_t>(value.range.direction));
    writer.u8(static_cast<std::uint8_t>(value.planes.size()));
    writer.u32(value.width);
    writer.u64(static_cast<std::uint64_t>(value.range.left));
    writer.u64(static_cast<std::uint64_t>(value.range.right));
    writer.u64(value.time_unit_fs);
    writer.u32(static_cast<std::uint32_t>(value.type_name.size()));
    writer.u32(static_cast<std::uint32_t>(value.enum_literals.size()));
    writer.u32(static_cast<std::uint32_t>(word_count(value.width)));
    writer.u32(0U);
    writer.text(value.type_name);
    for (const auto& literal : value.enum_literals) {
        writer.u32(static_cast<std::uint32_t>(literal.size()));
        writer.text(literal);
    }
    for (const auto& plane : value.planes) {
        for (const auto word : plane) {
            writer.u64(word);
        }
    }
    auto bytes = writer.take();
    if (bytes.size() != size) {
        report_error(diagnostics, SystemCKernelValueCode::payload,
            "SystemC value encoder produced an inconsistent byte count");
        return std::nullopt;
    }
    return bytes;
}

std::optional<SystemCKernelValue> deserialize_systemc_kernel_value(
    const std::span<const std::byte> bytes,
    const SystemCKernelValueLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)) {
        return std::nullopt;
    }
    if (bytes.size() < kHeaderBytes
        || bytes.size() > limits.max_encoded_bytes) {
        report_error(diagnostics, SystemCKernelValueCode::resource,
            "SystemC value payload is shorter than its header or exceeds its byte limit");
        return std::nullopt;
    }
    Reader reader { bytes };
    EncodedHeader header;
    SystemCKernelValue value;
    if (!read_encoded_header(reader, header, value)) {
        report_error(diagnostics, SystemCKernelValueCode::payload,
            "SystemC value header is malformed, truncated, or noncanonical");
        return std::nullopt;
    }
    if (!valid_encoded_header(header, value, limits, diagnostics)) {
        return std::nullopt;
    }
    if (!reader.text(header.type_size, value.type_name)) {
        report_error(diagnostics, SystemCKernelValueCode::payload,
            "SystemC value type name is truncated");
        return std::nullopt;
    }
    if (!read_enum_literals(
            reader, header.enum_count, value, limits, diagnostics)
        || !read_planes(reader, header, value, diagnostics)) {
        return std::nullopt;
    }
    if (reader.remaining() != 0U
        || !validate_systemc_kernel_value(value, limits, diagnostics)) {
        if (reader.remaining() != 0U) {
            report_error(diagnostics, SystemCKernelValueCode::payload,
                "SystemC value payload contains trailing bytes");
        }
        return std::nullopt;
    }
    return value;
}

std::optional<SystemCKernelValue> make_systemc_kernel_scalar_value(
    const SystemCKernelScalarProjection& scalar,
    const SystemCKernelValueLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (scalar.width == 0U || scalar.width > 64U
        || (scalar.width < 64U && (scalar.bits >> scalar.width) != 0U)) {
        report_error(diagnostics, SystemCKernelValueCode::lossy,
            "SystemC scalar projection is zero-width or has nonzero padding");
        return std::nullopt;
    }
    SystemCKernelValue value;
    value.width = scalar.width;
    value.is_signed = scalar.is_signed;
    value.range = { static_cast<std::int64_t>(scalar.width - 1U), 0,
        SystemCKernelRangeDirection::descending };
    value.planes = { { scalar.bits } };
    if (!validate_systemc_kernel_value(value, limits, diagnostics)) {
        return std::nullopt;
    }
    return value;
}

std::optional<SystemCKernelScalarProjection>
project_systemc_kernel_scalar_value(const SystemCKernelValue& value,
    const SystemCKernelValueLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!validate_systemc_kernel_value(value, limits, diagnostics)) {
        return std::nullopt;
    }
    if (value.kind != SystemCKernelValueKind::bit2 || value.width > 64U
        || value.range.direction
            != SystemCKernelRangeDirection::descending
        || value.range.left != static_cast<std::int64_t>(value.width - 1U)
        || value.range.right != 0 || !value.type_name.empty()) {
        report_error(diagnostics, SystemCKernelValueCode::lossy,
            "SystemC typed value cannot be projected to the legacy scalar without loss");
        return std::nullopt;
    }
    return SystemCKernelScalarProjection { value.planes[0][0],
        static_cast<std::uint16_t>(value.width), value.is_signed };
}

const char* systemc_kernel_value_diagnostic_code(
    const SystemCKernelValueCode code) noexcept
{
    switch (code) {
    case SystemCKernelValueCode::none:
        return "";
    case SystemCKernelValueCode::metadata:
        return "FSIM-SC-V001";
    case SystemCKernelValueCode::payload:
        return "FSIM-SC-V002";
    case SystemCKernelValueCode::resource:
        return "FSIM-SC-V003";
    case SystemCKernelValueCode::lossy:
        return "FSIM-SC-V004";
    }
    return "FSIM-SC-V001";
}

} // namespace fsim::systemc
