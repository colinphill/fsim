// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_database_codec.hpp"

#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <limits>
#include <new>
#include <ranges>
#include <string>
#include <system_error>
#include <utility>

namespace fsim::artifact {
namespace {

    inline constexpr std::array<char, 8> kNamespaceMagic {
        'F', 'S', 'I', 'M', 'C', 'V', 'N', '\0'
    };
    inline constexpr std::uint64_t kNamespaceHeaderBytes = 64U;

    class Writer {
    public:
        explicit Writer(const std::uint64_t limit) noexcept
            : limit_(limit)
        {
        }

        bool raw(const std::span<const std::byte> value)
        {
            if (value.size() > limit_ - std::min<std::uint64_t>(bytes_.size(), limit_)) {
                return false;
            }
            bytes_.insert(bytes_.end(), value.begin(), value.end());
            return true;
        }

        bool raw(const std::string_view value)
        {
            return raw(std::as_bytes(std::span { value.data(), value.size() }));
        }

        bool u8(const std::uint8_t value)
        {
            return raw(std::span { reinterpret_cast<const std::byte*>(&value), 1U });
        }

        bool u32(const std::uint32_t value)
        {
            std::array<std::byte, 4> bytes;
            for (std::size_t index = 0; index < bytes.size(); ++index) {
                bytes[index] = static_cast<std::byte>(
                    value >> ((bytes.size() - 1U - index) * 8U));
            }
            return raw(bytes);
        }

        bool u64(const std::uint64_t value)
        {
            std::array<std::byte, 8> bytes;
            for (std::size_t index = 0; index < bytes.size(); ++index) {
                bytes[index] = static_cast<std::byte>(
                    value >> ((bytes.size() - 1U - index) * 8U));
            }
            return raw(bytes);
        }

        bool text(const std::string_view value)
        {
            return value.size() <= std::numeric_limits<std::uint32_t>::max()
                && u32(static_cast<std::uint32_t>(value.size())) && raw(value);
        }

        bool identity(const CoverageDatabaseIdentity value)
        {
            return u64(value.high) && u64(value.low);
        }

        bool digest(const CoverageDatabaseDigest& value)
        {
            return raw(value);
        }

        bool patch_u64(const std::size_t offset, const std::uint64_t value) noexcept
        {
            if (offset > bytes_.size() || bytes_.size() - offset < 8U) {
                return false;
            }
            for (std::size_t index = 0; index < 8U; ++index) {
                bytes_[offset + index] = static_cast<std::byte>(
                    value >> ((7U - index) * 8U));
            }
            return true;
        }

        [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept
        {
            return bytes_;
        }

        [[nodiscard]] std::vector<std::byte> take() noexcept
        {
            return std::move(bytes_);
        }

    private:
        std::uint64_t limit_;
        std::vector<std::byte> bytes_;
    };

    class Reader {
    public:
        explicit Reader(const std::span<const std::byte> bytes) noexcept
            : bytes_(bytes)
        {
        }

        bool raw(const std::size_t count, std::span<const std::byte>& result) noexcept
        {
            if (offset_ > bytes_.size() || count > bytes_.size() - offset_) {
                return false;
            }
            result = bytes_.subspan(offset_, count);
            offset_ += count;
            return true;
        }

        bool u8(std::uint8_t& result) noexcept
        {
            std::span<const std::byte> bytes;
            if (!raw(1U, bytes)) {
                return false;
            }
            result = std::to_integer<std::uint8_t>(bytes.front());
            return true;
        }

        bool u32(std::uint32_t& result) noexcept
        {
            std::span<const std::byte> bytes;
            if (!raw(4U, bytes)) {
                return false;
            }
            result = 0U;
            for (const auto value : bytes) {
                result = (result << 8U) | std::to_integer<std::uint8_t>(value);
            }
            return true;
        }

        bool u64(std::uint64_t& result) noexcept
        {
            std::span<const std::byte> bytes;
            if (!raw(8U, bytes)) {
                return false;
            }
            result = 0U;
            for (const auto value : bytes) {
                result = (result << 8U) | std::to_integer<std::uint8_t>(value);
            }
            return true;
        }

        bool identity(CoverageDatabaseIdentity& result) noexcept
        {
            return u64(result.high) && u64(result.low);
        }

        bool digest(CoverageDatabaseDigest& result) noexcept
        {
            std::span<const std::byte> bytes;
            if (!raw(result.size(), bytes)) {
                return false;
            }
            std::ranges::copy(bytes, result.begin());
            return true;
        }

        bool text(std::string& result, const std::size_t limit)
        {
            std::uint32_t count { };
            std::span<const std::byte> bytes;
            if (!u32(count) || count > limit || !raw(count, bytes)) {
                return false;
            }
            if (bytes.empty()) {
                result.clear();
            } else {
                result.assign(
                    reinterpret_cast<const char*>(bytes.data()), bytes.size());
            }
            return true;
        }

        [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
        [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
        [[nodiscard]] bool done() const noexcept { return offset_ == bytes_.size(); }

    private:
        std::span<const std::byte> bytes_;
        std::size_t offset_ { };
    };

    CoverageDatabaseDigest digest_bytes(const std::span<const std::byte> bytes) noexcept
    {
        const auto source = support::Sha256::digest(bytes);
        CoverageDatabaseDigest result;
        for (std::size_t index = 0; index < result.size(); ++index) {
            result[index] = static_cast<std::byte>(source[index]);
        }
        return result;
    }

    bool namespace_header(Writer& writer, const CoverageDatabaseNamespace kind,
        const std::uint64_t sources, const std::uint64_t runs,
        const std::uint64_t metrics, const std::uint64_t exclusions)
    {
        return writer.raw(std::string_view { kNamespaceMagic.data(),
                   kNamespaceMagic.size() })
            && writer.u32(kCoverageDatabaseNamespaceSchema)
            && writer.u32(static_cast<std::uint32_t>(kind)) && writer.u64(0U)
            && writer.u64(sources) && writer.u64(runs) && writer.u64(metrics)
            && writer.u64(exclusions) && writer.u64(0U);
    }

    bool write_metric(Writer& writer, const CoverageDatabaseMetricRecord& metric)
    {
        const auto flags = static_cast<std::uint32_t>(metric.overflow ? 1U : 0U)
            | static_cast<std::uint32_t>(metric.excluded_overflow ? 2U : 0U);
        return writer.u32(static_cast<std::uint32_t>(metric.name_space))
            && writer.u8(static_cast<std::uint8_t>(metric.family))
            && writer.u8(static_cast<std::uint8_t>(metric.scope))
            && writer.u32(flags) && writer.identity(metric.bin_identity)
            && writer.identity(metric.source_identity)
            && writer.identity(metric.instance_identity)
            && writer.identity(metric.run_identity) && writer.u64(metric.hits)
            && writer.u64(metric.excluded_hits)
            && writer.u64(metric.source_line);
    }

    bool write_exclusion(
        Writer& writer, const CoverageDatabaseExclusionRecord& exclusion)
    {
        return writer.u32(static_cast<std::uint32_t>(exclusion.name_space))
            && writer.u8(static_cast<std::uint8_t>(exclusion.family))
            && writer.u8(static_cast<std::uint8_t>(exclusion.scope))
            && writer.u32(0U) && writer.identity(exclusion.point_identity)
            && writer.identity(exclusion.source_identity)
            && writer.identity(exclusion.instance_identity)
            && writer.u64(exclusion.source_line)
            && writer.text(exclusion.reason);
    }

    std::optional<std::vector<std::byte>> encode_namespace(
        const CoverageDatabaseContents& contents,
        const CoverageDatabaseNamespace kind,
        const CoverageDatabaseCodecLimits& limits)
    {
        const auto metrics = static_cast<std::uint64_t>(std::ranges::count(
            contents.metrics, kind, &CoverageDatabaseMetricRecord::name_space));
        const auto exclusions = static_cast<std::uint64_t>(std::ranges::count(
            contents.exclusions, kind,
            &CoverageDatabaseExclusionRecord::name_space));
        const auto common = kind == CoverageDatabaseNamespace::Code;
        Writer writer { limits.container.maximum_namespace_bytes };
        if (!namespace_header(writer, kind,
                common ? contents.sources.size() : 0U,
                common ? contents.runs.size() : 0U, metrics, exclusions)) {
            return std::nullopt;
        }
        if (common) {
            if (!writer.u32(contents.fingerprint.schema)
                || !writer.digest(contents.fingerprint.digest)
                || !writer.text(contents.fingerprint.model)) {
                return std::nullopt;
            }
            for (const auto& source : contents.sources) {
                if (!writer.identity(source.identity)
                    || !writer.u64(source.content_bytes)
                    || !writer.digest(source.content_digest)
                    || !writer.text(source.logical_path)) {
                    return std::nullopt;
                }
            }
            for (const auto& run : contents.runs) {
                if (!writer.identity(run.identity) || !writer.u64(run.seed)
                    || !writer.u64(run.final_tick) || !writer.u64(run.final_delta)
                    || !writer.u8(static_cast<std::uint8_t>(run.status))) {
                    return std::nullopt;
                }
                for (std::size_t index = 0; index < 7U; ++index) {
                    if (!writer.u8(0U)) {
                        return std::nullopt;
                    }
                }
                if (!writer.text(run.label) || !writer.text(run.producer)) {
                    return std::nullopt;
                }
            }
        }
        for (const auto& metric : contents.metrics) {
            if (metric.name_space == kind && !write_metric(writer, metric)) {
                return std::nullopt;
            }
        }
        for (const auto& exclusion : contents.exclusions) {
            if (exclusion.name_space == kind
                && !write_exclusion(writer, exclusion)) {
                return std::nullopt;
            }
        }
        if (!writer.patch_u64(16U, writer.bytes().size())) {
            return std::nullopt;
        }
        return writer.take();
    }

    bool write_schema(Writer& writer, const CoverageDatabaseSchema& schema)
    {
        if (!writer.raw(std::string_view {
                schema.header.magic.data(), schema.header.magic.size() })
            || !writer.u32(schema.header.schema)
            || !writer.u32(schema.header.header_bytes)
            || !writer.u32(schema.header.byte_order_marker)
            || !writer.u32(schema.header.flags)
            || !writer.u64(schema.header.container_bytes)
            || !writer.u64(schema.header.directory_offset)
            || !writer.u32(schema.header.directory_entry_bytes)
            || !writer.u32(schema.header.directory_entries)
            || !writer.u64(schema.header.directory_bytes)
            || !writer.u64(schema.header.reserved)) {
            return false;
        }
        for (const auto& descriptor : schema.namespaces) {
            if (!writer.u32(static_cast<std::uint32_t>(descriptor.kind))
                || !writer.u32(descriptor.schema)
                || !writer.u32(static_cast<std::uint32_t>(descriptor.encoding))
                || !writer.u32(descriptor.flags)
                || !writer.u64(descriptor.payload_offset)
                || !writer.u64(descriptor.payload_bytes)
                || !writer.digest(descriptor.payload_digest)) {
                return false;
            }
        }
        return true;
    }

    bool read_schema(Reader& reader, CoverageDatabaseSchema& schema) noexcept
    {
        std::span<const std::byte> magic;
        if (!reader.raw(schema.header.magic.size(), magic)) {
            return false;
        }
        for (std::size_t index = 0; index < magic.size(); ++index) {
            schema.header.magic[index]
                = static_cast<char>(std::to_integer<unsigned char>(magic[index]));
        }
        std::uint32_t encoding { };
        if (!reader.u32(schema.header.schema)
            || !reader.u32(schema.header.header_bytes)
            || !reader.u32(schema.header.byte_order_marker)
            || !reader.u32(schema.header.flags)
            || !reader.u64(schema.header.container_bytes)
            || !reader.u64(schema.header.directory_offset)
            || !reader.u32(schema.header.directory_entry_bytes)
            || !reader.u32(schema.header.directory_entries)
            || !reader.u64(schema.header.directory_bytes)
            || !reader.u64(schema.header.reserved)) {
            return false;
        }
        for (auto& descriptor : schema.namespaces) {
            std::uint32_t kind { };
            if (!reader.u32(kind) || !reader.u32(descriptor.schema)
                || !reader.u32(encoding) || !reader.u32(descriptor.flags)
                || !reader.u64(descriptor.payload_offset)
                || !reader.u64(descriptor.payload_bytes)
                || !reader.digest(descriptor.payload_digest)) {
                return false;
            }
            descriptor.kind = static_cast<CoverageDatabaseNamespace>(kind);
            descriptor.encoding = static_cast<CoverageDatabaseEncoding>(encoding);
        }
        return reader.offset() == kCoverageDatabasePayloadOffset;
    }

    CoverageDatabaseDecodeResult malformed(const std::size_t offset) noexcept
    {
        return { { }, CoverageDatabaseCodecError::Malformed,
            CoverageDatabaseModelError::None, CoverageDatabaseSchemaError::None,
            offset };
    }

    bool admit_count(const std::uint64_t count, const std::size_t current,
        const std::size_t maximum) noexcept
    {
        return count <= maximum && current <= maximum - static_cast<std::size_t>(count);
    }

    bool add_product(std::uint64_t& total, const std::uint64_t count,
        const std::uint64_t bytes) noexcept
    {
        if (count != 0U
            && bytes > std::numeric_limits<std::uint64_t>::max() / count) {
            return false;
        }
        const auto product = count * bytes;
        if (product > std::numeric_limits<std::uint64_t>::max() - total) {
            return false;
        }
        total += product;
        return true;
    }

    bool read_namespace_header(Reader& reader, const CoverageDatabaseNamespace kind,
        std::uint64_t& sources, std::uint64_t& runs, std::uint64_t& metrics,
        std::uint64_t& exclusions) noexcept
    {
        std::span<const std::byte> magic;
        std::uint32_t schema { };
        std::uint32_t encoded_kind { };
        std::uint64_t payload_bytes { };
        std::uint64_t reserved { };
        if (!reader.raw(kNamespaceMagic.size(), magic)
            || !std::ranges::equal(magic,
                std::as_bytes(std::span {
                    kNamespaceMagic.data(), kNamespaceMagic.size() }))
            || !reader.u32(schema) || !reader.u32(encoded_kind)
            || !reader.u64(payload_bytes) || !reader.u64(sources)
            || !reader.u64(runs) || !reader.u64(metrics)
            || !reader.u64(exclusions) || !reader.u64(reserved)) {
            return false;
        }
        return schema == kCoverageDatabaseNamespaceSchema
            && encoded_kind == static_cast<std::uint32_t>(kind)
            && payload_bytes == reader.size()
            && reader.offset() == kNamespaceHeaderBytes && reserved == 0U;
    }

    bool read_metric(Reader& reader, CoverageDatabaseMetricRecord& metric) noexcept
    {
        std::uint32_t name_space { };
        std::uint8_t family { };
        std::uint8_t scope { };
        std::uint32_t flags { };
        if (!reader.u32(name_space) || !reader.u8(family) || !reader.u8(scope)
            || !reader.u32(flags) || flags > 3U
            || !reader.identity(metric.bin_identity)
            || !reader.identity(metric.source_identity)
            || !reader.identity(metric.instance_identity)
            || !reader.identity(metric.run_identity) || !reader.u64(metric.hits)
            || !reader.u64(metric.excluded_hits)
            || !reader.u64(metric.source_line)) {
            return false;
        }
        metric.name_space = static_cast<CoverageDatabaseNamespace>(name_space);
        metric.family = static_cast<CoverageDatabaseMetricFamily>(family);
        metric.scope = static_cast<CoverageDatabaseMetricScope>(scope);
        metric.overflow = (flags & 1U) != 0U;
        metric.excluded_overflow = (flags & 2U) != 0U;
        return true;
    }

    bool read_exclusion(Reader& reader, CoverageDatabaseExclusionRecord& exclusion,
        const CoverageDatabaseModelLimits& limits)
    {
        std::uint32_t name_space { };
        std::uint8_t family { };
        std::uint8_t scope { };
        std::uint32_t reserved { };
        if (!reader.u32(name_space) || !reader.u8(family) || !reader.u8(scope)
            || !reader.u32(reserved) || reserved != 0U
            || !reader.identity(exclusion.point_identity)
            || !reader.identity(exclusion.source_identity)
            || !reader.identity(exclusion.instance_identity)
            || !reader.u64(exclusion.source_line)
            || !reader.text(exclusion.reason, limits.maximum_reason_bytes)) {
            return false;
        }
        exclusion.name_space = static_cast<CoverageDatabaseNamespace>(name_space);
        exclusion.family = static_cast<CoverageDatabaseMetricFamily>(family);
        exclusion.scope = static_cast<CoverageDatabaseMetricScope>(scope);
        return true;
    }

    CoverageDatabaseCodecError decode_namespace(
        const std::span<const std::byte> bytes,
        const CoverageDatabaseNamespace kind, CoverageDatabaseContents& contents,
        const CoverageDatabaseModelLimits& limits, std::size_t& failure_offset)
    {
        Reader reader { bytes };
        std::uint64_t source_count { };
        std::uint64_t run_count { };
        std::uint64_t metric_count { };
        std::uint64_t exclusion_count { };
        if (!read_namespace_header(reader, kind, source_count, run_count,
                metric_count, exclusion_count)) {
            failure_offset = reader.offset();
            return CoverageDatabaseCodecError::Malformed;
        }
        const auto common = kind == CoverageDatabaseNamespace::Code;
        if ((!common && (source_count != 0U || run_count != 0U))
            || !admit_count(source_count, contents.sources.size(),
                limits.maximum_sources)
            || !admit_count(run_count, contents.runs.size(), limits.maximum_runs)
            || !admit_count(metric_count, contents.metrics.size(),
                limits.maximum_metrics)
            || !admit_count(exclusion_count, contents.exclusions.size(),
                limits.maximum_exclusions)) {
            failure_offset = reader.offset();
            return CoverageDatabaseCodecError::ResourceLimit;
        }
        std::uint64_t minimum_bytes = common ? 40U : 0U;
        if (!add_product(minimum_bytes, source_count, 60U)
            || !add_product(minimum_bytes, run_count, 56U)
            || !add_product(minimum_bytes, metric_count, 98U)
            || !add_product(minimum_bytes, exclusion_count, 70U)
            || minimum_bytes > bytes.size() - reader.offset()) {
            failure_offset = reader.offset();
            return CoverageDatabaseCodecError::Malformed;
        }
        if (common) {
            if (!reader.u32(contents.fingerprint.schema)
                || !reader.digest(contents.fingerprint.digest)
                || !reader.text(contents.fingerprint.model,
                    limits.maximum_label_bytes)) {
                failure_offset = reader.offset();
                return CoverageDatabaseCodecError::Malformed;
            }
            contents.sources.reserve(static_cast<std::size_t>(source_count));
            for (std::uint64_t index = 0; index < source_count; ++index) {
                CoverageDatabaseSourceRecord source;
                if (!reader.identity(source.identity)
                    || !reader.u64(source.content_bytes)
                    || !reader.digest(source.content_digest)
                    || !reader.text(
                        source.logical_path, limits.maximum_logical_path_bytes)) {
                    failure_offset = reader.offset();
                    return CoverageDatabaseCodecError::Malformed;
                }
                contents.sources.push_back(std::move(source));
            }
            contents.runs.reserve(static_cast<std::size_t>(run_count));
            for (std::uint64_t index = 0; index < run_count; ++index) {
                CoverageDatabaseRunRecord run;
                std::uint8_t status { };
                if (!reader.identity(run.identity) || !reader.u64(run.seed)
                    || !reader.u64(run.final_tick) || !reader.u64(run.final_delta)
                    || !reader.u8(status)) {
                    failure_offset = reader.offset();
                    return CoverageDatabaseCodecError::Malformed;
                }
                for (std::size_t reserved = 0; reserved < 7U; ++reserved) {
                    std::uint8_t value { };
                    if (!reader.u8(value) || value != 0U) {
                        failure_offset = reader.offset();
                        return CoverageDatabaseCodecError::Malformed;
                    }
                }
                run.status = static_cast<CoverageDatabaseRunStatus>(status);
                if (!reader.text(run.label, limits.maximum_label_bytes)
                    || !reader.text(run.producer, limits.maximum_producer_bytes)) {
                    failure_offset = reader.offset();
                    return CoverageDatabaseCodecError::Malformed;
                }
                contents.runs.push_back(std::move(run));
            }
        }
        contents.metrics.reserve(
            contents.metrics.size() + static_cast<std::size_t>(metric_count));
        for (std::uint64_t index = 0; index < metric_count; ++index) {
            CoverageDatabaseMetricRecord metric;
            if (!read_metric(reader, metric) || metric.name_space != kind) {
                failure_offset = reader.offset();
                return CoverageDatabaseCodecError::Malformed;
            }
            contents.metrics.push_back(metric);
        }
        contents.exclusions.reserve(
            contents.exclusions.size() + static_cast<std::size_t>(exclusion_count));
        for (std::uint64_t index = 0; index < exclusion_count; ++index) {
            CoverageDatabaseExclusionRecord exclusion;
            if (!read_exclusion(reader, exclusion, limits)
                || exclusion.name_space != kind) {
                failure_offset = reader.offset();
                return CoverageDatabaseCodecError::Malformed;
            }
            contents.exclusions.push_back(std::move(exclusion));
        }
        if (!reader.done()) {
            failure_offset = reader.offset();
            return CoverageDatabaseCodecError::Malformed;
        }
        return CoverageDatabaseCodecError::None;
    }

} // namespace

CoverageDatabaseEncodeResult serialize_coverage_database(
    CoverageDatabaseContents contents,
    const CoverageDatabaseCodecLimits& limits) noexcept
{
    try {
        auto made = make_coverage_database_contents(
            std::move(contents), limits.model);
        if (!made.ok()) {
            return { { }, CoverageDatabaseCodecError::InvalidModel,
                made.error, CoverageDatabaseSchemaError::None };
        }
        std::array<std::vector<std::byte>, kCoverageDatabaseNamespaceCount>
            payloads;
        std::array<CoverageDatabaseNamespaceExtent,
            kCoverageDatabaseNamespaceCount>
            extents;
        const std::array kinds { CoverageDatabaseNamespace::Code,
            CoverageDatabaseNamespace::SystemVerilogFunctional,
            CoverageDatabaseNamespace::Psl };
        for (std::size_t index = 0; index < kinds.size(); ++index) {
            auto payload = encode_namespace(*made.contents, kinds[index], limits);
            if (!payload) {
                return { { }, CoverageDatabaseCodecError::ResourceLimit,
                    CoverageDatabaseModelError::None,
                    CoverageDatabaseSchemaError::None };
            }
            payloads[index] = std::move(*payload);
            extents[index] = { payloads[index].size(),
                digest_bytes(payloads[index]) };
        }
        const auto schema = make_coverage_database_schema(extents, limits.container);
        if (!schema.ok()) {
            return { { }, CoverageDatabaseCodecError::InvalidSchema,
                CoverageDatabaseModelError::None, schema.error };
        }
        Writer writer { limits.container.maximum_container_bytes };
        if (!write_schema(writer, schema.schema)) {
            return { { }, CoverageDatabaseCodecError::ResourceLimit,
                CoverageDatabaseModelError::None,
                CoverageDatabaseSchemaError::None };
        }
        for (const auto& payload : payloads) {
            if (!writer.raw(payload)) {
                return { { }, CoverageDatabaseCodecError::ResourceLimit,
                    CoverageDatabaseModelError::None,
                    CoverageDatabaseSchemaError::None };
            }
        }
        return { writer.take(), CoverageDatabaseCodecError::None,
            CoverageDatabaseModelError::None,
            CoverageDatabaseSchemaError::None };
    } catch (const std::bad_alloc&) {
        return { { }, CoverageDatabaseCodecError::AllocationFailure,
            CoverageDatabaseModelError::None,
            CoverageDatabaseSchemaError::None };
    }
}

CoverageDatabaseDecodeResult deserialize_coverage_database(
    const std::span<const std::byte> bytes,
    const CoverageDatabaseCodecLimits& limits) noexcept
{
    try {
        if (bytes.size() > limits.container.maximum_container_bytes) {
            return { { }, CoverageDatabaseCodecError::ResourceLimit };
        }
        Reader reader { bytes };
        CoverageDatabaseSchema schema;
        if (!read_schema(reader, schema)) {
            return malformed(reader.offset());
        }
        const auto schema_error
            = validate_coverage_database_schema(schema, limits.container);
        if (schema_error != CoverageDatabaseSchemaError::None) {
            return { { }, CoverageDatabaseCodecError::InvalidSchema,
                CoverageDatabaseModelError::None, schema_error,
                reader.offset() };
        }
        if (schema.header.container_bytes != bytes.size()) {
            return malformed(reader.offset());
        }
        CoverageDatabaseContents contents;
        for (const auto& descriptor : schema.namespaces) {
            if (descriptor.payload_offset > bytes.size()
                || descriptor.payload_bytes
                    > bytes.size()
                        - static_cast<std::size_t>(descriptor.payload_offset)) {
                return malformed(reader.offset());
            }
            const auto payload = bytes.subspan(
                static_cast<std::size_t>(descriptor.payload_offset),
                static_cast<std::size_t>(descriptor.payload_bytes));
            if (digest_bytes(payload) != descriptor.payload_digest) {
                return { { }, CoverageDatabaseCodecError::DigestMismatch,
                    CoverageDatabaseModelError::None,
                    CoverageDatabaseSchemaError::None,
                    static_cast<std::size_t>(descriptor.payload_offset) };
            }
            std::size_t failure_offset { };
            const auto decode_error = decode_namespace(
                payload, descriptor.kind, contents, limits.model,
                failure_offset);
            if (decode_error != CoverageDatabaseCodecError::None) {
                if (decode_error == CoverageDatabaseCodecError::ResourceLimit) {
                    return { { }, decode_error,
                        CoverageDatabaseModelError::None,
                        CoverageDatabaseSchemaError::None,
                        static_cast<std::size_t>(descriptor.payload_offset)
                            + failure_offset };
                }
                return malformed(static_cast<std::size_t>(
                                     descriptor.payload_offset)
                    + failure_offset);
            }
        }
        const auto validation
            = validate_coverage_database_contents(contents, limits.model);
        if (!validation.ok()) {
            return { { }, CoverageDatabaseCodecError::InvalidModel,
                validation.error, CoverageDatabaseSchemaError::None,
                validation.index };
        }
        return { std::move(contents), CoverageDatabaseCodecError::None,
            CoverageDatabaseModelError::None,
            CoverageDatabaseSchemaError::None, 0U };
    } catch (const std::bad_alloc&) {
        return { { }, CoverageDatabaseCodecError::AllocationFailure };
    }
}

CoverageDatabaseFileResult write_coverage_database_atomically(
    const std::filesystem::path& path, CoverageDatabaseContents contents,
    const CoverageDatabaseCodecLimits& limits) noexcept
{
    const auto encoded = serialize_coverage_database(std::move(contents), limits);
    if (!encoded.ok()) {
        return { encoded.error };
    }
    std::error_code error;
    if (path.empty()) {
        return { CoverageDatabaseCodecError::IoFailure };
    }
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return { CoverageDatabaseCodecError::IoFailure };
        }
    }
    auto temporary = path;
    temporary += ".fsim-tmp";
    auto backup = path;
    backup += ".fsim-old";
    const auto destination_exists = std::filesystem::exists(path, error);
    if (error) {
        return { CoverageDatabaseCodecError::IoFailure };
    }
    const auto backup_exists = std::filesystem::exists(backup, error);
    if (error) {
        return { CoverageDatabaseCodecError::IoFailure };
    }
    if (!destination_exists && backup_exists) {
        std::filesystem::rename(backup, path, error);
        if (error) {
            return { CoverageDatabaseCodecError::AtomicReplaceFailure };
        }
    } else if (backup_exists) {
        std::filesystem::remove(backup, error);
        if (error) {
            return { CoverageDatabaseCodecError::IoFailure };
        }
    }
    std::filesystem::remove(temporary, error);
    if (error) {
        return { CoverageDatabaseCodecError::IoFailure };
    }
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return { CoverageDatabaseCodecError::IoFailure };
        }
        output.write(reinterpret_cast<const char*>(encoded.bytes.data()),
            static_cast<std::streamsize>(encoded.bytes.size()));
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, error);
            return { CoverageDatabaseCodecError::IoFailure };
        }
    }
    const auto exists = std::filesystem::exists(path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return { CoverageDatabaseCodecError::IoFailure };
    }
    if (exists) {
        std::filesystem::rename(path, backup, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return { CoverageDatabaseCodecError::AtomicReplaceFailure };
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        if (exists) {
            std::error_code restore_error;
            std::filesystem::rename(backup, path, restore_error);
        }
        std::filesystem::remove(temporary, error);
        return { CoverageDatabaseCodecError::AtomicReplaceFailure };
    }
    if (exists) {
        std::filesystem::remove(backup, error);
        if (error) {
            return { CoverageDatabaseCodecError::IoFailure };
        }
    }
    return { CoverageDatabaseCodecError::None };
}

CoverageDatabaseDecodeResult read_coverage_database(
    const std::filesystem::path& path,
    const CoverageDatabaseCodecLimits& limits) noexcept
{
    try {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error) {
            return { { }, CoverageDatabaseCodecError::IoFailure };
        }
        if (size > limits.container.maximum_container_bytes
            || size > std::numeric_limits<std::size_t>::max()) {
            return { { }, CoverageDatabaseCodecError::ResourceLimit };
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return { { }, CoverageDatabaseCodecError::IoFailure };
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        input.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!input && !bytes.empty()) {
            return { { }, CoverageDatabaseCodecError::IoFailure };
        }
        return deserialize_coverage_database(bytes, limits);
    } catch (const std::bad_alloc&) {
        return { { }, CoverageDatabaseCodecError::AllocationFailure };
    }
}

} // namespace fsim::artifact
