// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_artifact_identity.hpp"
#include "fsim/library/artifact.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::app {

struct SdfPortableNormalizedRecord {
    bool cell { };
    std::uint64_t id { };
    std::uint64_t cell_id { };
    std::uint64_t parent_id { };
    std::size_t sibling_index { };
    std::size_t depth { };
    std::uint32_t kind { };
    std::string source_identity;
    std::string canonical_identity;
    std::string profile_identity;
    std::string exact_value;
    std::string scaled_femtoseconds;

    friend bool operator==(const SdfPortableNormalizedRecord&,
        const SdfPortableNormalizedRecord&) = default;
};

struct SdfPortableMappingRecord {
    bool unit { };
    std::uint64_t node_id { };
    std::uint64_t cell_id { };
    std::uint64_t declaration_or_signal { };
    std::string target_instance_path;
    std::string unit_or_object_identity;
    std::string semantic_identity;

    friend bool operator==(const SdfPortableMappingRecord&,
        const SdfPortableMappingRecord&) = default;
};

class SdfPortableArchiveSnapshot final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    [[nodiscard]] const artifact::DesignSdfAnnotation& annotation() const
        noexcept;
    [[nodiscard]] std::span<const std::byte> schema_envelope() const noexcept;
    [[nodiscard]] std::span<const SdfPortableNormalizedRecord> normalized()
        const noexcept;
    [[nodiscard]] std::span<const SdfPortableMappingRecord> mappings() const
        noexcept;
    [[nodiscard]] std::string_view payload_checksum() const noexcept;

    SdfPortableArchiveSnapshot(artifact::DesignSdfAnnotation annotation,
        std::vector<std::byte> schema_envelope,
        std::vector<SdfPortableNormalizedRecord> normalized,
        std::vector<SdfPortableMappingRecord> mappings,
        std::string payload_checksum);

private:
    artifact::DesignSdfAnnotation annotation_;
    std::vector<std::byte> schema_envelope_;
    std::vector<SdfPortableNormalizedRecord> normalized_;
    std::vector<SdfPortableMappingRecord> mappings_;
    std::string payload_checksum_;
};

struct SdfPortableArchiveLimits {
    std::size_t max_bytes { 256U << 20U };
    std::size_t max_normalized_records { 1U << 22U };
    std::size_t max_mapping_records { 1U << 23U };
    std::size_t max_string_bytes { 64U << 20U };
};

struct SdfPortableArchiveEncodeResult {
    std::vector<std::byte> bytes;
    std::shared_ptr<const SdfPortableArchiveSnapshot> snapshot;
    std::vector<frontend::Diagnostic> diagnostics;
    [[nodiscard]] bool ok() const noexcept;
};

struct SdfPortableArchiveDecodeResult {
    std::shared_ptr<const SdfPortableArchiveSnapshot> snapshot;
    std::vector<frontend::Diagnostic> diagnostics;
    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfPortableArchiveEncodeResult encode_sdf_portable_archive(
    const SdfSchemaSnapshot& schema,
    const SdfAnnotationSummary& summary,
    const artifact::DesignSdfAnnotation& annotation,
    std::span<const std::byte> schema_envelope,
    SdfPortableArchiveLimits limits = { });

[[nodiscard]] SdfPortableArchiveDecodeResult decode_sdf_portable_archive(
    std::span<const std::byte> bytes,
    const artifact::DesignSdfAnnotation& expected_annotation,
    SdfPortableArchiveLimits limits = { });

[[nodiscard]] library::UnitIndexEntry make_sdf_library_index_entry(
    const artifact::DesignSdfAnnotation& annotation,
    const std::filesystem::path& artifact_path,
    std::span<const std::byte> bytes);

[[nodiscard]] artifact::DesignPayload make_sdf_design_payload(
    const artifact::DesignSdfAnnotation& annotation,
    const std::filesystem::path& artifact_path,
    std::span<const std::byte> bytes);

[[nodiscard]] SdfPortableArchiveDecodeResult load_sdf_library_archive(
    const std::filesystem::path& library_directory,
    const library::Metadata& metadata,
    const artifact::DesignSdfAnnotation& expected_annotation,
    SdfPortableArchiveLimits limits = { });

} // namespace fsim::app
