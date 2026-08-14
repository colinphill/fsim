// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/sdf_portable_archive.hpp"

#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <ranges>
#include <type_traits>
#include <utility>

namespace fsim::app {
namespace {
    constexpr std::array<std::byte, 8> magic { std::byte { 'F' }, std::byte { 'S' },
        std::byte { 'D' }, std::byte { 'F' }, std::byte { 'P' }, std::byte { 'O' },
        std::byte { 'R' }, std::byte { 'T' } };

    void diagnose(std::vector<frontend::Diagnostic>& diagnostics, std::string code,
        std::string message, const frontend::SourceSpan& span = { })
    {
        diagnostics.push_back(frontend::Diagnostic {
            frontend::DiagnosticSeverity::Error, std::move(code),
            std::move(message), span, { } });
    }

    std::string checksum(const std::span<const std::byte> bytes)
    {
        const auto text = std::string_view {
            reinterpret_cast<const char*>(bytes.data()), bytes.size()
        };
        return support::Sha256::hex(support::Sha256::digest(text));
    }

    class Writer {
    public:
        void u8(const std::uint8_t value) { bytes_.push_back(std::byte { value }); }
        void u32(const std::uint32_t value)
        {
            for (unsigned shift = 0; shift != 32U; shift += 8U)
                u8(static_cast<std::uint8_t>(value >> shift));
        }
        void u64(const std::uint64_t value)
        {
            for (unsigned shift = 0; shift != 64U; shift += 8U)
                u8(static_cast<std::uint8_t>(value >> shift));
        }
        void boolean(const bool value) { u8(value ? 1U : 0U); }
        void string(const std::string_view value)
        {
            u64(value.size());
            const auto* first = reinterpret_cast<const std::byte*>(value.data());
            bytes_.insert(bytes_.end(), first, first + value.size());
        }
        void blob(const std::span<const std::byte> value)
        {
            u64(value.size());
            bytes_.insert(bytes_.end(), value.begin(), value.end());
        }
        void raw(const std::span<const std::byte> value)
        {
            bytes_.insert(bytes_.end(), value.begin(), value.end());
        }
        [[nodiscard]] std::span<const std::byte> view() const noexcept
        {
            return bytes_;
        }
        [[nodiscard]] std::vector<std::byte> take() && { return std::move(bytes_); }

    private:
        std::vector<std::byte> bytes_;
    };

    class Reader {
    public:
        Reader(const std::span<const std::byte> bytes, const std::size_t string_limit)
            : bytes_(bytes)
            , string_limit_(string_limit)
        {
        }
        bool raw(const std::span<const std::byte> value)
        {
            if (remaining() < value.size()
                || !std::ranges::equal(bytes_.subspan(position_, value.size()), value))
                return false;
            position_ += value.size();
            return true;
        }
        std::optional<std::uint8_t> u8()
        {
            if (remaining() == 0U)
                return std::nullopt;
            return std::to_integer<std::uint8_t>(bytes_[position_++]);
        }
        std::optional<std::uint32_t> u32()
        {
            if (remaining() < 4U)
                return std::nullopt;
            std::uint32_t result { };
            for (unsigned shift = 0; shift != 32U; shift += 8U)
                result |= static_cast<std::uint32_t>(*u8()) << shift;
            return result;
        }
        std::optional<std::uint64_t> u64()
        {
            if (remaining() < 8U)
                return std::nullopt;
            std::uint64_t result { };
            for (unsigned shift = 0; shift != 64U; shift += 8U)
                result |= static_cast<std::uint64_t>(*u8()) << shift;
            return result;
        }
        std::optional<bool> boolean()
        {
            const auto value = u8();
            if (!value || *value > 1U)
                return std::nullopt;
            return *value != 0U;
        }
        std::optional<std::string> string()
        {
            const auto size = u64();
            if (!size || *size > remaining() || *size > string_limit_)
                return std::nullopt;
            const auto count = static_cast<std::size_t>(*size);
            std::string result(reinterpret_cast<const char*>(bytes_.data() + position_),
                count);
            position_ += count;
            return result;
        }
        std::optional<std::vector<std::byte>> blob()
        {
            const auto size = u64();
            if (!size || *size > remaining())
                return std::nullopt;
            const auto count = static_cast<std::size_t>(*size);
            const auto begin = bytes_.begin()
                + static_cast<std::ptrdiff_t>(position_);
            std::vector<std::byte> result(
                begin, begin + static_cast<std::ptrdiff_t>(count));
            position_ += count;
            return result;
        }
        [[nodiscard]] std::size_t position() const noexcept { return position_; }
        [[nodiscard]] std::size_t remaining() const noexcept
        {
            return bytes_.size() - position_;
        }

    private:
        std::span<const std::byte> bytes_;
        std::size_t string_limit_ { };
        std::size_t position_ { };
    };

    template <typename Values, typename Callback>
    void write_sequence(Writer& writer, const Values& values, Callback&& callback)
    {
        writer.u64(values.size());
        for (const auto& value : values)
            callback(value);
    }

    template <typename Callback>
    bool read_sequence(Reader& reader, const std::size_t limit, Callback&& callback)
    {
        const auto count = reader.u64();
        if (!count || *count > limit)
            return false;
        for (std::uint64_t index = 0; index != *count; ++index) {
            if (!callback())
                return false;
        }
        return true;
    }

    void write_annotation(
        Writer& writer, const artifact::DesignSdfAnnotation& annotation)
    {
        writer.u32(annotation.schema);
        writer.string(annotation.revision);
        writer.string(annotation.revision_adapter);
        writer.boolean(annotation.has_timescale);
        writer.string(annotation.timescale);
        writer.string(annotation.selection_policy);
        writer.string(annotation.scope_identity);
        writer.string(annotation.source_digest);
        writer.string(annotation.design_digest);
        writer.string(annotation.ir_identity);
        writer.string(annotation.resolution_identity);
        writer.string(annotation.mapping_identity);
        write_sequence(writer, annotation.selected_root_identities,
            [&](const auto& value) { writer.string(value); });
        write_sequence(writer, annotation.semantic_unit_identities,
            [&](const auto& value) { writer.string(value); });
        write_sequence(writer, annotation.semantic_object_identities,
            [&](const auto& value) { writer.string(value); });
        writer.string(annotation.cache_key);
    }

    bool read_strings(Reader& reader, std::vector<std::string>& values)
    {
        return read_sequence(reader, 1U << 23U, [&] {
            auto value = reader.string();
            if (value)
                values.push_back(std::move(*value));
            return value.has_value();
        });
    }

    bool read_annotation(Reader& reader, artifact::DesignSdfAnnotation& annotation)
    {
        const auto schema = reader.u32();
        auto revision = reader.string();
        auto adapter = reader.string();
        const auto has_timescale = reader.boolean();
        auto timescale = reader.string();
        auto selection = reader.string();
        auto scope = reader.string();
        auto source = reader.string();
        auto design = reader.string();
        auto ir = reader.string();
        auto resolution = reader.string();
        auto mapping = reader.string();
        if (!schema || !revision || !adapter || !has_timescale || !timescale
            || !selection || !scope || !source || !design || !ir || !resolution
            || !mapping)
            return false;
        annotation.schema = *schema;
        annotation.revision = std::move(*revision);
        annotation.revision_adapter = std::move(*adapter);
        annotation.has_timescale = *has_timescale;
        annotation.timescale = std::move(*timescale);
        annotation.selection_policy = std::move(*selection);
        annotation.scope_identity = std::move(*scope);
        annotation.source_digest = std::move(*source);
        annotation.design_digest = std::move(*design);
        annotation.ir_identity = std::move(*ir);
        annotation.resolution_identity = std::move(*resolution);
        annotation.mapping_identity = std::move(*mapping);
        if (!read_strings(reader, annotation.selected_root_identities)
            || !read_strings(reader, annotation.semantic_unit_identities)
            || !read_strings(reader, annotation.semantic_object_identities))
            return false;
        auto cache = reader.string();
        if (!cache)
            return false;
        annotation.cache_key = std::move(*cache);
        return true;
    }

    void write_normalized(
        Writer& writer, const SdfPortableNormalizedRecord& record)
    {
        writer.boolean(record.cell);
        writer.u64(record.id);
        writer.u64(record.cell_id);
        writer.u64(record.parent_id);
        writer.u64(record.sibling_index);
        writer.u64(record.depth);
        writer.u32(record.kind);
        writer.string(record.source_identity);
        writer.string(record.canonical_identity);
        writer.string(record.profile_identity);
        writer.string(record.exact_value);
        writer.string(record.scaled_femtoseconds);
    }

    bool read_normalized(Reader& reader, SdfPortableNormalizedRecord& record)
    {
        const auto cell = reader.boolean();
        const auto id = reader.u64();
        const auto cell_id = reader.u64();
        const auto parent_id = reader.u64();
        const auto sibling = reader.u64();
        const auto depth = reader.u64();
        const auto kind = reader.u32();
        auto source = reader.string();
        auto canonical = reader.string();
        auto profile = reader.string();
        auto exact = reader.string();
        auto scaled = reader.string();
        if (!cell || !id || !cell_id || !parent_id || !sibling || !depth || !kind
            || !source || !canonical || !profile || !exact || !scaled)
            return false;
        record = { *cell, *id, *cell_id, *parent_id,
            static_cast<std::size_t>(*sibling), static_cast<std::size_t>(*depth),
            *kind, std::move(*source), std::move(*canonical), std::move(*profile),
            std::move(*exact), std::move(*scaled) };
        return true;
    }

    void write_mapping(Writer& writer, const SdfPortableMappingRecord& record)
    {
        writer.boolean(record.unit);
        writer.u64(record.node_id);
        writer.u64(record.cell_id);
        writer.u64(record.declaration_or_signal);
        writer.string(record.target_instance_path);
        writer.string(record.unit_or_object_identity);
        writer.string(record.semantic_identity);
    }

    bool read_mapping(Reader& reader, SdfPortableMappingRecord& record)
    {
        const auto unit = reader.boolean();
        const auto node = reader.u64();
        const auto cell = reader.u64();
        const auto object = reader.u64();
        auto target = reader.string();
        auto identity = reader.string();
        auto semantic = reader.string();
        if (!unit || !node || !cell || !object || !target || !identity || !semantic)
            return false;
        record = { *unit, *node, *cell, *object, std::move(*target),
            std::move(*identity), std::move(*semantic) };
        return true;
    }

    std::vector<SdfPortableNormalizedRecord> normalized_records(
        const frontend::SdfIr& ir)
    {
        std::vector<SdfPortableNormalizedRecord> result;
        result.reserve(ir.cells().size() + ir.nodes().size());
        for (const auto& cell : ir.cells()) {
            result.push_back({ true, cell.id, cell.id, 0U, cell.first_node,
                cell.node_count, static_cast<std::uint32_t>(cell.instance_kind),
                cell.source_identity, cell.canonical_identity, { }, { }, { } });
        }
        for (const auto& node : ir.nodes()) {
            result.push_back({ false, node.id, node.cell_id, node.parent_id,
                node.sibling_index, node.depth,
                static_cast<std::uint32_t>(node.kind), node.source_identity,
                node.canonical_identity, node.profile_identity,
                node.exact_value ? node.exact_value->canonical : std::string { },
                node.scaled_femtoseconds
                    ? node.scaled_femtoseconds->canonical
                    : std::string { } });
        }
        return result;
    }

    std::vector<SdfPortableMappingRecord> mapping_records(
        const SdfAnnotationSummary& summary)
    {
        std::vector<SdfPortableMappingRecord> result;
        const auto& endpoints = summary.endpoint_resolution();
        for (const auto& cell : endpoints->cells()->cells()) {
            for (const auto& target : cell.targets) {
                result.push_back({ true, 0U, cell.cell_id, target.declaration_id,
                    target.instance_path, target.unit_identity,
                    target.unit_identity });
            }
        }
        for (const auto& node : endpoints->nodes()) {
            for (const auto& endpoint : node.endpoints) {
                result.push_back({ false, node.node_id, node.cell_id,
                    endpoint.signal, node.target_instance_path,
                    endpoint.instance_path + '\0' + endpoint.object_path,
                    compute_sdf_semantic_object_identity(node, endpoint) });
            }
        }
        return result;
    }

    bool valid_mapping_identities(const SdfPortableArchiveSnapshot& snapshot)
    {
        for (const auto& mapping : snapshot.mappings()) {
            const auto& identities = mapping.unit
                ? snapshot.annotation().semantic_unit_identities
                : snapshot.annotation().semantic_object_identities;
            if (!std::ranges::binary_search(identities, mapping.semantic_identity))
                return false;
        }
        return true;
    }

} // namespace

SdfPortableArchiveSnapshot::SdfPortableArchiveSnapshot(
    artifact::DesignSdfAnnotation annotation,
    std::vector<std::byte> schema_envelope,
    std::vector<SdfPortableNormalizedRecord> normalized,
    std::vector<SdfPortableMappingRecord> mappings,
    std::string payload_checksum)
    : annotation_(std::move(annotation))
    , schema_envelope_(std::move(schema_envelope))
    , normalized_(std::move(normalized))
    , mappings_(std::move(mappings))
    , payload_checksum_(std::move(payload_checksum))
{
}

const artifact::DesignSdfAnnotation&
SdfPortableArchiveSnapshot::annotation() const noexcept
{
    return annotation_;
}
std::span<const std::byte>
SdfPortableArchiveSnapshot::schema_envelope() const noexcept
{
    return schema_envelope_;
}
std::span<const SdfPortableNormalizedRecord>
SdfPortableArchiveSnapshot::normalized() const noexcept
{
    return normalized_;
}
std::span<const SdfPortableMappingRecord>
SdfPortableArchiveSnapshot::mappings() const noexcept
{
    return mappings_;
}
std::string_view SdfPortableArchiveSnapshot::payload_checksum() const noexcept
{
    return payload_checksum_;
}

bool SdfPortableArchiveEncodeResult::ok() const noexcept
{
    return !bytes.empty() && snapshot && diagnostics.empty();
}
bool SdfPortableArchiveDecodeResult::ok() const noexcept
{
    return snapshot && diagnostics.empty();
}

SdfPortableArchiveEncodeResult encode_sdf_portable_archive(
    const SdfSchemaSnapshot& schema, const SdfAnnotationSummary& summary,
    const artifact::DesignSdfAnnotation& annotation,
    const std::span<const std::byte> schema_envelope,
    const SdfPortableArchiveLimits limits)
{
    SdfPortableArchiveEncodeResult result;
    const auto decoded_schema = decode_sdf_schema(schema_envelope,
        schema.options().compiler_compatibility_identity,
        summary.semantic_identity());
    if (!decoded_schema.ok()
        || decoded_schema.snapshot->envelope_checksum()
            != schema.envelope_checksum()
        || annotation.ir_identity != schema.ir_semantic_identity()
        || annotation.resolution_identity
            != schema.resolution_semantic_identity()
        || annotation.mapping_identity != schema.summary_semantic_identity()
        || annotation.cache_key != compute_sdf_artifact_cache_key(annotation)) {
        diagnose(result.diagnostics, "FSIM-SDF-PORTABLE-001",
            "portable SDF archive requires compatible complete schema, mapping, "
            "annotation, and cache identities",
            schema.source_span());
        return result;
    }
    auto normalized = normalized_records(
        *summary.endpoint_resolution()->cells()->scope()->normalized_ir());
    auto mappings = mapping_records(summary);
    if (normalized.size() > limits.max_normalized_records
        || mappings.size() > limits.max_mapping_records) {
        diagnose(result.diagnostics, "FSIM-SDF-PORTABLE-004",
            "portable SDF archive exceeds normalized or mapping record limits",
            schema.source_span());
        return result;
    }
    Writer writer;
    writer.raw(magic);
    writer.u32(SdfPortableArchiveSnapshot::schema_version);
    write_annotation(writer, annotation);
    writer.blob(schema_envelope);
    write_sequence(writer, normalized,
        [&](const auto& record) { write_normalized(writer, record); });
    write_sequence(writer, mappings,
        [&](const auto& record) { write_mapping(writer, record); });
    const auto payload_checksum = checksum(writer.view());
    writer.string(payload_checksum);
    auto bytes = std::move(writer).take();
    if (bytes.size() > limits.max_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-PORTABLE-004",
            "portable SDF archive exceeds its byte limit", schema.source_span());
        return result;
    }
    result.snapshot = std::make_shared<const SdfPortableArchiveSnapshot>(
        annotation, std::vector<std::byte>(schema_envelope.begin(), schema_envelope.end()),
        std::move(normalized), std::move(mappings), payload_checksum);
    result.bytes = std::move(bytes);
    return result;
}

SdfPortableArchiveDecodeResult decode_sdf_portable_archive(
    const std::span<const std::byte> bytes,
    const artifact::DesignSdfAnnotation& expected_annotation,
    const SdfPortableArchiveLimits limits)
{
    SdfPortableArchiveDecodeResult result;
    if (bytes.size() > limits.max_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-PORTABLE-004",
            "portable SDF archive exceeds its byte limit");
        return result;
    }
    Reader reader(bytes, limits.max_string_bytes);
    const auto version = reader.raw(magic) ? reader.u32() : std::nullopt;
    artifact::DesignSdfAnnotation annotation;
    std::vector<SdfPortableNormalizedRecord> normalized;
    std::vector<SdfPortableMappingRecord> mappings;
    if (!version || *version != SdfPortableArchiveSnapshot::schema_version
        || !read_annotation(reader, annotation)) {
        diagnose(result.diagnostics, "FSIM-SDF-PORTABLE-002",
            "portable SDF archive has invalid magic, version, or annotation");
        return result;
    }
    auto envelope = reader.blob();
    if (!envelope
        || !read_sequence(reader, limits.max_normalized_records, [&] {
               SdfPortableNormalizedRecord record;
               if (!read_normalized(reader, record))
                   return false;
               normalized.push_back(std::move(record));
               return true;
           })
        || !read_sequence(reader, limits.max_mapping_records, [&] {
               SdfPortableMappingRecord record;
               if (!read_mapping(reader, record))
                   return false;
               mappings.push_back(std::move(record));
               return true;
           })) {
        diagnose(result.diagnostics, "FSIM-SDF-PORTABLE-002",
            "portable SDF archive is truncated or contains invalid records");
        return result;
    }
    const auto checksum_offset = reader.position();
    auto stored_checksum = reader.string();
    if (!stored_checksum || reader.remaining() != 0U
        || *stored_checksum != checksum(bytes.first(checksum_offset))) {
        diagnose(result.diagnostics, "FSIM-SDF-PORTABLE-002",
            "portable SDF archive checksum is invalid or has trailing bytes");
        return result;
    }
    if (annotation != expected_annotation) {
        diagnose(result.diagnostics, "FSIM-SDF-PORTABLE-003",
            "portable SDF archive is incompatible with the selected design or "
            "annotation identity");
        return result;
    }
    auto snapshot = std::make_shared<const SdfPortableArchiveSnapshot>(
        std::move(annotation), std::move(*envelope), std::move(normalized),
        std::move(mappings), std::move(*stored_checksum));
    if (!valid_mapping_identities(*snapshot)) {
        diagnose(result.diagnostics, "FSIM-SDF-PORTABLE-002",
            "portable SDF archive mapping records do not match their archived "
            "semantic identities");
        return result;
    }
    result.snapshot = std::move(snapshot);
    return result;
}

library::UnitIndexEntry make_sdf_library_index_entry(
    const artifact::DesignSdfAnnotation& annotation,
    const std::filesystem::path& artifact_path,
    const std::span<const std::byte> bytes)
{
    return { "sdf", "annotation", annotation.cache_key, { }, { },
        artifact_path, checksum(bytes), annotation.revision,
        "fsim-sdf-portable-v1" };
}

artifact::DesignPayload make_sdf_design_payload(
    const artifact::DesignSdfAnnotation& annotation,
    const std::filesystem::path& artifact_path,
    const std::span<const std::byte> bytes)
{
    return { "sdf:" + annotation.cache_key, artifact_path, checksum(bytes) };
}

SdfPortableArchiveDecodeResult load_sdf_library_archive(
    const std::filesystem::path& library_directory,
    const library::Metadata& metadata,
    const artifact::DesignSdfAnnotation& expected_annotation,
    const SdfPortableArchiveLimits limits)
{
    SdfPortableArchiveDecodeResult result;
    const auto unit = std::ranges::find_if(metadata.units, [&](const auto& item) {
        return item.language == "sdf" && item.kind == "annotation"
            && item.name == expected_annotation.cache_key
            && item.standard == expected_annotation.revision
            && item.compatibility_profile == "fsim-sdf-portable-v1";
    });
    if (unit == metadata.units.end()) {
        diagnose(result.diagnostics, "FSIM-SDF-PORTABLE-003",
            "mapped library does not contain a compatible SDF annotation");
        return result;
    }
    std::ifstream input(library_directory / unit->artifact, std::ios::binary);
    if (!input) {
        diagnose(result.diagnostics, "FSIM-SDF-PORTABLE-003",
            "mapped library SDF annotation payload cannot be opened");
        return result;
    }
    std::vector<std::byte> bytes;
    char character { };
    while (input.get(character)) {
        if (bytes.size() >= limits.max_bytes) {
            diagnose(result.diagnostics, "FSIM-SDF-PORTABLE-004",
                "mapped library SDF annotation payload exceeds its byte limit");
            return result;
        }
        bytes.push_back(std::byte { static_cast<unsigned char>(character) });
    }
    if (!input.eof() || checksum(bytes) != unit->checksum) {
        diagnose(result.diagnostics, "FSIM-SDF-PORTABLE-003",
            "mapped library SDF annotation payload checksum is invalid");
        return result;
    }
    return decode_sdf_portable_archive(bytes, expected_annotation, limits);
}

} // namespace fsim::app
