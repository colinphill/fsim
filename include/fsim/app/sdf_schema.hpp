// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_mapping_validation.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

struct SdfSchemaOptions {
    std::string parse_identity;
    std::string normalization_identity;
    std::string compiler_compatibility_identity;

    friend bool operator==(const SdfSchemaOptions&,
        const SdfSchemaOptions&) = default;
};

struct SdfSchemaHeader {
    frontend::SdfHeaderKind kind { frontend::SdfHeaderKind::SdfVersion };
    std::string keyword_spelling;
    std::vector<std::string> value_spellings;
    std::string canonical_value;
    frontend::SourceSpan span;

    friend bool operator==(const SdfSchemaHeader&,
        const SdfSchemaHeader&) = default;
};

class SdfSchemaSnapshot final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    [[nodiscard]] frontend::SdfRevision revision() const noexcept;
    [[nodiscard]] frontend::SdfRevisionAdapter revision_adapter() const
        noexcept;
    [[nodiscard]] std::string_view source_checksum() const noexcept;
    [[nodiscard]] const SdfSchemaOptions& options() const noexcept;
    [[nodiscard]] const frontend::SourceSpan& source_span() const noexcept;
    [[nodiscard]] std::span<const SdfSchemaHeader> headers() const noexcept;
    [[nodiscard]] std::string_view ir_semantic_identity() const noexcept;
    [[nodiscard]] std::string_view resolution_semantic_identity() const
        noexcept;
    [[nodiscard]] std::string_view summary_semantic_identity() const noexcept;
    [[nodiscard]] std::size_t cell_count() const noexcept;
    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] std::size_t mapping_count() const noexcept;
    [[nodiscard]] std::string_view envelope_checksum() const noexcept;

    SdfSchemaSnapshot(frontend::SdfRevision revision,
        frontend::SdfRevisionAdapter revision_adapter,
        std::string source_checksum, SdfSchemaOptions options,
        frontend::SourceSpan source_span, std::vector<SdfSchemaHeader> headers,
        std::string ir_semantic_identity,
        std::string resolution_semantic_identity,
        std::string summary_semantic_identity, std::size_t cell_count,
        std::size_t node_count, std::size_t mapping_count,
        std::string envelope_checksum);

private:
    frontend::SdfRevision revision_ { frontend::SdfRevision::Sdf40 };
    frontend::SdfRevisionAdapter revision_adapter_ {
        frontend::SdfRevisionAdapter::None
    };
    std::string source_checksum_;
    SdfSchemaOptions options_;
    frontend::SourceSpan source_span_;
    std::vector<SdfSchemaHeader> headers_;
    std::string ir_semantic_identity_;
    std::string resolution_semantic_identity_;
    std::string summary_semantic_identity_;
    std::size_t cell_count_ { };
    std::size_t node_count_ { };
    std::size_t mapping_count_ { };
    std::string envelope_checksum_;
};

struct SdfSchemaLimits {
    std::size_t max_bytes { 16U << 20U };
    std::size_t max_headers { 64U };
    std::size_t max_strings { 1'000'000U };
    std::size_t max_string_bytes { 1U << 20U };
};

struct SdfSchemaEncodeResult {
    std::vector<std::byte> bytes;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

struct SdfSchemaDecodeResult {
    std::shared_ptr<const SdfSchemaSnapshot> snapshot;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfSchemaEncodeResult encode_sdf_schema(
    const frontend::SdfFile& file, const SdfAnnotationSummary& summary,
    std::string_view source_text, const SdfSchemaOptions& options,
    SdfSchemaLimits limits = { });

[[nodiscard]] SdfSchemaDecodeResult decode_sdf_schema(
    std::span<const std::byte> bytes,
    std::string_view expected_compiler_compatibility_identity,
    std::string_view expected_summary_semantic_identity = { },
    SdfSchemaLimits limits = { });

} // namespace fsim::app
