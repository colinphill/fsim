// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_schema.hpp"

#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <utility>

namespace fsim::app {
namespace {
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::SourceLocation;
    using frontend::SourceSpan;

    constexpr std::uint16_t kChecksumRecord = 14U;

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message, const SourceSpan& span = { })
    {
        diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), span, { } });
    }

    class Writer final {
    public:
        void u16(const std::uint16_t value)
        {
            bytes_.push_back(static_cast<std::byte>(value & 0xffU));
            bytes_.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
        }

        void u32(const std::uint32_t value)
        {
            for (unsigned shift = 0U; shift < 32U; shift += 8U)
                bytes_.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }

        void u64(const std::uint64_t value)
        {
            for (unsigned shift = 0U; shift < 64U; shift += 8U)
                bytes_.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }

        bool string(const std::string_view value, const SdfSchemaLimits& limits)
        {
            if (value.size() > limits.max_string_bytes
                || value.size() > std::numeric_limits<std::uint32_t>::max()) {
                return false;
            }
            u32(static_cast<std::uint32_t>(value.size()));
            bytes_.insert(bytes_.end(),
                reinterpret_cast<const std::byte*>(value.data()),
                reinterpret_cast<const std::byte*>(value.data() + value.size()));
            return true;
        }

        bool record(const std::uint16_t id, Writer payload)
        {
            if (payload.bytes_.size()
                > std::numeric_limits<std::uint32_t>::max()) {
                return false;
            }
            u16(id);
            u32(static_cast<std::uint32_t>(payload.bytes_.size()));
            bytes_.insert(bytes_.end(), payload.bytes_.begin(), payload.bytes_.end());
            return true;
        }

        [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept
        {
            return bytes_;
        }

        [[nodiscard]] std::vector<std::byte> take() &&
        {
            return std::move(bytes_);
        }

    private:
        std::vector<std::byte> bytes_;
    };

    class Reader final {
    public:
        explicit Reader(const std::span<const std::byte> bytes)
            : bytes_(bytes)
        {
        }

        [[nodiscard]] std::optional<std::uint16_t> u16()
        {
            if (remaining() < 2U)
                return std::nullopt;
            const auto result = static_cast<std::uint16_t>(value(cursor_))
                | static_cast<std::uint16_t>(value(cursor_ + 1U) << 8U);
            cursor_ += 2U;
            return result;
        }

        [[nodiscard]] std::optional<std::uint32_t> u32()
        {
            if (remaining() < 4U)
                return std::nullopt;
            std::uint32_t result = 0U;
            for (unsigned index = 0U; index < 4U; ++index)
                result |= static_cast<std::uint32_t>(value(cursor_ + index))
                    << (index * 8U);
            cursor_ += 4U;
            return result;
        }

        [[nodiscard]] std::optional<std::uint64_t> u64()
        {
            if (remaining() < 8U)
                return std::nullopt;
            std::uint64_t result = 0U;
            for (unsigned index = 0U; index < 8U; ++index)
                result |= static_cast<std::uint64_t>(value(cursor_ + index))
                    << (index * 8U);
            cursor_ += 8U;
            return result;
        }

        [[nodiscard]] std::optional<std::string> string(
            const SdfSchemaLimits& limits, std::size_t& string_count)
        {
            const auto size = u32();
            if (!size || *size > limits.max_string_bytes || *size > remaining()
                || string_count >= limits.max_strings) {
                return std::nullopt;
            }
            const auto* begin
                = reinterpret_cast<const char*>(bytes_.data() + cursor_);
            std::string result(begin, begin + *size);
            cursor_ += *size;
            ++string_count;
            return result;
        }

        [[nodiscard]] std::optional<Reader> record(
            const std::uint16_t expected_id)
        {
            const auto id = u16();
            const auto size = u32();
            if (!id || !size || *id != expected_id || *size > remaining())
                return std::nullopt;
            Reader result(bytes_.subspan(cursor_, *size));
            cursor_ += *size;
            return result;
        }

        [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }
        [[nodiscard]] bool empty() const noexcept { return cursor_ == bytes_.size(); }
        [[nodiscard]] std::size_t remaining() const noexcept
        {
            return bytes_.size() - cursor_;
        }

    private:
        [[nodiscard]] unsigned value(const std::size_t index) const noexcept
        {
            return std::to_integer<unsigned>(bytes_[index]);
        }

        std::span<const std::byte> bytes_;
        std::size_t cursor_ { };
    };

    [[nodiscard]] bool write_location(
        Writer& writer, const SourceLocation& location)
    {
        writer.u64(location.offset);
        writer.u64(location.line);
        writer.u64(location.column);
        return true;
    }

    [[nodiscard]] bool write_span(Writer& writer, const SourceSpan& span,
        const SdfSchemaLimits& limits)
    {
        if (!writer.string(span.source_name, limits)
            || !write_location(writer, span.begin)
            || !write_location(writer, span.end)
            || !writer.string(span.physical_source_name, limits)
            || span.expansion_stack.size() > limits.max_strings) {
            return false;
        }
        writer.u32(static_cast<std::uint32_t>(span.expansion_stack.size()));
        return std::ranges::all_of(span.expansion_stack,
            [&](const std::string& entry) { return writer.string(entry, limits); });
    }

    [[nodiscard]] std::optional<SourceLocation> read_location(Reader& reader)
    {
        const auto offset = reader.u64();
        const auto line = reader.u64();
        const auto column = reader.u64();
        if (!offset || !line || !column
            || *offset > std::numeric_limits<std::size_t>::max()
            || *line > std::numeric_limits<std::size_t>::max()
            || *column > std::numeric_limits<std::size_t>::max()) {
            return std::nullopt;
        }
        return SourceLocation { static_cast<std::size_t>(*offset),
            static_cast<std::size_t>(*line), static_cast<std::size_t>(*column) };
    }

    [[nodiscard]] std::optional<SourceSpan> read_span(Reader& reader,
        const SdfSchemaLimits& limits, std::size_t& string_count)
    {
        auto source_name = reader.string(limits, string_count);
        const auto begin = read_location(reader);
        const auto end = read_location(reader);
        auto physical = reader.string(limits, string_count);
        const auto expansion_count = reader.u32();
        if (!source_name || !begin || !end || !physical || !expansion_count
            || *expansion_count > limits.max_strings) {
            return std::nullopt;
        }
        SourceSpan result;
        result.source_name = std::move(*source_name);
        result.begin = *begin;
        result.end = *end;
        result.physical_source_name = std::move(*physical);
        result.expansion_stack.reserve(*expansion_count);
        for (std::uint32_t index = 0U; index < *expansion_count; ++index) {
            auto entry = reader.string(limits, string_count);
            if (!entry)
                return std::nullopt;
            result.expansion_stack.push_back(std::move(*entry));
        }
        return result;
    }

    [[nodiscard]] bool write_headers(Writer& writer,
        const std::span<const frontend::SdfHeaderRecord> headers,
        const SdfSchemaLimits& limits)
    {
        if (headers.size() > limits.max_headers)
            return false;
        writer.u32(static_cast<std::uint32_t>(headers.size()));
        for (const auto& header : headers) {
            writer.u32(static_cast<std::uint32_t>(header.kind));
            if (!writer.string(header.keyword_spelling, limits)
                || header.value_spellings.size() > limits.max_strings) {
                return false;
            }
            writer.u32(static_cast<std::uint32_t>(header.value_spellings.size()));
            for (const auto& value : header.value_spellings) {
                if (!writer.string(value, limits))
                    return false;
            }
            if (!writer.string(header.canonical_value, limits)
                || !write_span(writer, header.span, limits)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::optional<std::vector<SdfSchemaHeader>> read_headers(
        Reader& reader, const SdfSchemaLimits& limits,
        std::size_t& string_count)
    {
        const auto count = reader.u32();
        if (!count || *count > limits.max_headers)
            return std::nullopt;
        std::vector<SdfSchemaHeader> result;
        result.reserve(*count);
        for (std::uint32_t index = 0U; index < *count; ++index) {
            const auto kind = reader.u32();
            auto keyword = reader.string(limits, string_count);
            const auto value_count = reader.u32();
            if (!kind || *kind > static_cast<std::uint32_t>(frontend::SdfHeaderKind::Timescale)
                || !keyword || !value_count
                || *value_count > limits.max_strings) {
                return std::nullopt;
            }
            SdfSchemaHeader header;
            header.kind = static_cast<frontend::SdfHeaderKind>(*kind);
            header.keyword_spelling = std::move(*keyword);
            header.value_spellings.reserve(*value_count);
            for (std::uint32_t value = 0U; value < *value_count; ++value) {
                auto spelling = reader.string(limits, string_count);
                if (!spelling)
                    return std::nullopt;
                header.value_spellings.push_back(std::move(*spelling));
            }
            auto canonical = reader.string(limits, string_count);
            auto span = read_span(reader, limits, string_count);
            if (!canonical || !span)
                return std::nullopt;
            header.canonical_value = std::move(*canonical);
            header.span = std::move(*span);
            result.push_back(std::move(header));
        }
        return result;
    }

    [[nodiscard]] bool valid_sha256(const std::string_view value)
    {
        return value.size() == 64U && std::ranges::all_of(value, [](const char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        });
    }

    [[nodiscard]] std::string digest(
        const std::span<const std::byte> bytes)
    {
        return support::Sha256::hex(support::Sha256::digest(bytes));
    }

    [[nodiscard]] bool add_string_record(Writer& writer, const std::uint16_t id,
        const std::string_view value, const SdfSchemaLimits& limits)
    {
        Writer payload;
        return payload.string(value, limits)
            && writer.record(id, std::move(payload));
    }
} // namespace

SdfSchemaSnapshot::SdfSchemaSnapshot(const frontend::SdfRevision revision,
    const frontend::SdfRevisionAdapter revision_adapter,
    std::string source_checksum, SdfSchemaOptions options,
    frontend::SourceSpan source_span, std::vector<SdfSchemaHeader> headers,
    std::string ir_semantic_identity,
    std::string resolution_semantic_identity,
    std::string summary_semantic_identity, const std::size_t cell_count,
    const std::size_t node_count, const std::size_t mapping_count,
    std::string envelope_checksum)
    : revision_(revision)
    , revision_adapter_(revision_adapter)
    , source_checksum_(std::move(source_checksum))
    , options_(std::move(options))
    , source_span_(std::move(source_span))
    , headers_(std::move(headers))
    , ir_semantic_identity_(std::move(ir_semantic_identity))
    , resolution_semantic_identity_(std::move(resolution_semantic_identity))
    , summary_semantic_identity_(std::move(summary_semantic_identity))
    , cell_count_(cell_count)
    , node_count_(node_count)
    , mapping_count_(mapping_count)
    , envelope_checksum_(std::move(envelope_checksum))
{
}

frontend::SdfRevision SdfSchemaSnapshot::revision() const noexcept { return revision_; }
frontend::SdfRevisionAdapter SdfSchemaSnapshot::revision_adapter() const noexcept { return revision_adapter_; }
std::string_view SdfSchemaSnapshot::source_checksum() const noexcept { return source_checksum_; }
const SdfSchemaOptions& SdfSchemaSnapshot::options() const noexcept { return options_; }
const SourceSpan& SdfSchemaSnapshot::source_span() const noexcept { return source_span_; }
std::span<const SdfSchemaHeader> SdfSchemaSnapshot::headers() const noexcept { return headers_; }
std::string_view SdfSchemaSnapshot::ir_semantic_identity() const noexcept { return ir_semantic_identity_; }
std::string_view SdfSchemaSnapshot::resolution_semantic_identity() const noexcept { return resolution_semantic_identity_; }
std::string_view SdfSchemaSnapshot::summary_semantic_identity() const noexcept { return summary_semantic_identity_; }
std::size_t SdfSchemaSnapshot::cell_count() const noexcept { return cell_count_; }
std::size_t SdfSchemaSnapshot::node_count() const noexcept { return node_count_; }
std::size_t SdfSchemaSnapshot::mapping_count() const noexcept { return mapping_count_; }
std::string_view SdfSchemaSnapshot::envelope_checksum() const noexcept { return envelope_checksum_; }

bool SdfSchemaEncodeResult::ok() const noexcept
{
    return !bytes.empty() && !frontend::has_errors(diagnostics);
}

bool SdfSchemaDecodeResult::ok() const noexcept
{
    return snapshot && !frontend::has_errors(diagnostics);
}

SdfSchemaEncodeResult encode_sdf_schema(const frontend::SdfFile& file,
    const SdfAnnotationSummary& summary, const std::string_view source_text,
    const SdfSchemaOptions& options, const SdfSchemaLimits limits)
{
    SdfSchemaEncodeResult result;
    if (!file.has_revision || !file.normalized_ir
        || !summary.endpoint_resolution()
        || options.parse_identity.empty()
        || options.normalization_identity.empty()
        || options.compiler_compatibility_identity.empty()) {
        diagnose(result.diagnostics, "FSIM-SDF-SCHEMA-001",
            "SDF schema encoding requires complete syntax, IR, mapping, options, and compiler identity",
            file.span);
        return result;
    }
    const auto& resolution = *summary.endpoint_resolution();
    if (!resolution.cells() || !resolution.cells()->scope()
        || resolution.cells()->scope()->normalized_ir().get()
            != file.normalized_ir.get()
        || summary.semantic_identity().empty()) {
        diagnose(result.diagnostics, "FSIM-SDF-SCHEMA-001",
            "SDF schema encoding encountered stale syntax, IR, or mapping ownership",
            file.span);
        return result;
    }
    Writer writer;
    const auto resource_failure = [&]() {
        diagnose(result.diagnostics, "FSIM-SDF-SCHEMA-005",
            "SDF schema encoding exceeds a configured byte, header, string, or field limit",
            file.span);
        return result;
    };
    if (!add_string_record(writer, 1U, "FSIMSDF", limits))
        return resource_failure();
    for (std::uint16_t id = 2U; id <= 6U; ++id) {
        Writer payload;
        payload.u32(1U);
        if (!writer.record(id, std::move(payload)))
            return resource_failure();
    }
    {
        Writer payload;
        payload.u32(static_cast<std::uint32_t>(file.revision));
        payload.u32(static_cast<std::uint32_t>(file.revision_adapter));
        if (!writer.record(7U, std::move(payload)))
            return resource_failure();
    }
    if (!add_string_record(writer, 8U,
            support::Sha256::hex(support::Sha256::digest(source_text)), limits))
        return resource_failure();
    {
        Writer payload;
        if (!payload.string(options.parse_identity, limits)
            || !payload.string(options.normalization_identity, limits)
            || !payload.string(options.compiler_compatibility_identity, limits)
            || !writer.record(9U, std::move(payload)))
            return resource_failure();
    }
    {
        Writer payload;
        if (!write_span(payload, file.span, limits)
            || !writer.record(10U, std::move(payload)))
            return resource_failure();
    }
    {
        Writer payload;
        if (!write_headers(payload, file.headers, limits)
            || !writer.record(11U, std::move(payload)))
            return resource_failure();
    }
    {
        Writer payload;
        if (!payload.string(file.normalized_ir->semantic_identity(), limits)
            || !payload.string(resolution.semantic_identity(), limits)
            || !payload.string(summary.semantic_identity(), limits)
            || !writer.record(12U, std::move(payload)))
            return resource_failure();
    }
    {
        Writer payload;
        payload.u64(file.normalized_ir->cells().size());
        payload.u64(file.normalized_ir->nodes().size());
        payload.u64(resolution.nodes().size());
        if (!writer.record(13U, std::move(payload)))
            return resource_failure();
    }
    {
        const auto payload_checksum = digest(writer.bytes());
        if (!add_string_record(writer, kChecksumRecord, payload_checksum, limits))
            return resource_failure();
    }
    if (writer.bytes().size() > limits.max_bytes)
        return resource_failure();
    result.bytes = std::move(writer).take();
    return result;
}

SdfSchemaDecodeResult decode_sdf_schema(
    const std::span<const std::byte> bytes,
    const std::string_view expected_compiler_compatibility_identity,
    const std::string_view expected_summary_semantic_identity,
    const SdfSchemaLimits limits)
{
    SdfSchemaDecodeResult result;
    if (bytes.empty() || bytes.size() > limits.max_bytes
        || expected_compiler_compatibility_identity.empty()) {
        diagnose(result.diagnostics, "FSIM-SDF-SCHEMA-005",
            "SDF schema input or compatibility expectation exceeds configured limits");
        return result;
    }
    Reader reader(bytes);
    std::size_t strings = 0U;
    const auto corrupt = [&]() {
        diagnose(result.diagnostics, "FSIM-SDF-SCHEMA-004",
            "SDF schema is omitted, duplicate, reordered, truncated, corrupt, or contains an invalid field");
        return result;
    };
    auto read_string_record = [&](const std::uint16_t id)
        -> std::optional<std::string> {
        auto record = reader.record(id);
        if (!record)
            return std::nullopt;
        auto value = record->string(limits, strings);
        return value && record->empty() ? std::move(value) : std::nullopt;
    };
    const auto magic = read_string_record(1U);
    if (!magic || *magic != "FSIMSDF")
        return corrupt();
    for (std::uint16_t id = 2U; id <= 6U; ++id) {
        auto record = reader.record(id);
        const auto version = record ? record->u32() : std::nullopt;
        if (!version || !record->empty())
            return corrupt();
        if (*version > 1U) {
            diagnose(result.diagnostics, "FSIM-SDF-SCHEMA-002",
                "SDF schema contains an unsupported future schema version");
            return result;
        }
        if (*version != 1U)
            return corrupt();
    }
    frontend::SdfRevision revision;
    frontend::SdfRevisionAdapter adapter;
    {
        auto record = reader.record(7U);
        const auto raw_revision = record ? record->u32() : std::nullopt;
        const auto raw_adapter = record ? record->u32() : std::nullopt;
        if (!raw_revision || !raw_adapter || !record->empty()
            || *raw_revision
                > static_cast<std::uint32_t>(frontend::SdfRevision::Sdf40)
            || *raw_adapter > static_cast<std::uint32_t>(
                   frontend::SdfRevisionAdapter::Sdf30))
            return corrupt();
        revision = static_cast<frontend::SdfRevision>(*raw_revision);
        adapter = static_cast<frontend::SdfRevisionAdapter>(*raw_adapter);
    }
    auto source_checksum = read_string_record(8U);
    if (!source_checksum || !valid_sha256(*source_checksum))
        return corrupt();
    SdfSchemaOptions options;
    {
        auto record = reader.record(9U);
        auto parse = record ? record->string(limits, strings) : std::nullopt;
        auto normalization
            = record ? record->string(limits, strings) : std::nullopt;
        auto compiler = record ? record->string(limits, strings) : std::nullopt;
        if (!parse || !normalization || !compiler || !record->empty())
            return corrupt();
        options = { std::move(*parse), std::move(*normalization),
            std::move(*compiler) };
    }
    SourceSpan source_span;
    {
        auto record = reader.record(10U);
        auto span = record ? read_span(*record, limits, strings) : std::nullopt;
        if (!span || !record->empty())
            return corrupt();
        source_span = std::move(*span);
    }
    std::vector<SdfSchemaHeader> headers;
    {
        auto record = reader.record(11U);
        auto decoded
            = record ? read_headers(*record, limits, strings) : std::nullopt;
        if (!decoded || !record->empty())
            return corrupt();
        headers = std::move(*decoded);
    }
    std::string ir_identity;
    std::string resolution_identity;
    std::string summary_identity_value;
    {
        auto record = reader.record(12U);
        auto ir = record ? record->string(limits, strings) : std::nullopt;
        auto resolution
            = record ? record->string(limits, strings) : std::nullopt;
        auto summary = record ? record->string(limits, strings) : std::nullopt;
        if (!ir || !resolution || !summary || !record->empty()
            || ir->empty() || resolution->empty() || summary->empty())
            return corrupt();
        ir_identity = std::move(*ir);
        resolution_identity = std::move(*resolution);
        summary_identity_value = std::move(*summary);
    }
    std::size_t cell_count = 0U;
    std::size_t node_count = 0U;
    std::size_t mapping_count = 0U;
    {
        auto record = reader.record(13U);
        const auto cells = record ? record->u64() : std::nullopt;
        const auto nodes = record ? record->u64() : std::nullopt;
        const auto mappings = record ? record->u64() : std::nullopt;
        if (!cells || !nodes || !mappings || !record->empty()
            || *cells > std::numeric_limits<std::size_t>::max()
            || *nodes > std::numeric_limits<std::size_t>::max()
            || *mappings > std::numeric_limits<std::size_t>::max())
            return corrupt();
        cell_count = static_cast<std::size_t>(*cells);
        node_count = static_cast<std::size_t>(*nodes);
        mapping_count = static_cast<std::size_t>(*mappings);
    }
    {
        const auto checksum_offset = reader.cursor();
        auto checksum = read_string_record(kChecksumRecord);
        if (!checksum || !valid_sha256(*checksum) || !reader.empty()
            || *checksum != digest(bytes.first(checksum_offset)))
            return corrupt();
        if (options.compiler_compatibility_identity
            != expected_compiler_compatibility_identity) {
            diagnose(result.diagnostics, "FSIM-SDF-SCHEMA-003",
                "SDF schema compiler compatibility identity is stale");
            return result;
        }
        if (!expected_summary_semantic_identity.empty()
            && summary_identity_value
                != expected_summary_semantic_identity) {
            diagnose(result.diagnostics, "FSIM-SDF-SCHEMA-003",
                "SDF schema resolved-mapping semantic identity is stale");
            return result;
        }
        result.snapshot = std::make_shared<const SdfSchemaSnapshot>(revision,
            adapter, std::move(*source_checksum), std::move(options),
            std::move(source_span), std::move(headers), std::move(ir_identity),
            std::move(resolution_identity), std::move(summary_identity_value),
            cell_count, node_count, mapping_count, std::move(*checksum));
    }
    return result;
}

} // namespace fsim::app
