// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fsim::artifact {

inline constexpr std::array<char, 8> kCoverageDatabaseMagic {
    'F', 'S', 'I', 'M', 'C', 'O', 'V', '\0'
};
inline constexpr std::uint32_t kCoverageDatabaseSchema = 3U;
inline constexpr std::uint32_t kCoverageDatabaseNamespaceSchema = 3U;
inline constexpr std::uint32_t kCoverageDatabaseByteOrderMarker = 0x01020304U;
inline constexpr std::uint32_t kCoverageDatabaseHeaderBytes = 64U;
inline constexpr std::uint32_t kCoverageDatabaseNamespaceDescriptorBytes = 64U;
inline constexpr std::uint32_t kCoverageDatabaseNamespaceCount = 3U;
inline constexpr std::uint64_t kCoverageDatabaseDirectoryOffset
    = kCoverageDatabaseHeaderBytes;
inline constexpr std::uint64_t kCoverageDatabaseDirectoryBytes
    = static_cast<std::uint64_t>(kCoverageDatabaseNamespaceDescriptorBytes)
    * kCoverageDatabaseNamespaceCount;
inline constexpr std::uint64_t kCoverageDatabasePayloadOffset
    = kCoverageDatabaseDirectoryOffset + kCoverageDatabaseDirectoryBytes;
inline constexpr std::string_view kCoverageDatabaseDiagnostic = "FSIM-COV-031";

using CoverageDatabaseDigest = std::array<std::byte, 32>;

enum class CoverageDatabaseNamespace : std::uint32_t {
    Code = 1U,
    SystemVerilogFunctional = 2U,
    Psl = 3U,
};

enum class CoverageDatabaseEncoding : std::uint32_t {
    Raw = 0U,
};

struct CoverageDatabaseLimits {
    std::uint64_t maximum_container_bytes { 1ULL << 30U };
    std::uint64_t maximum_namespace_bytes { 512ULL << 20U };
    std::uint64_t maximum_directory_bytes { 1ULL << 20U };
    std::uint32_t maximum_namespaces { kCoverageDatabaseNamespaceCount };
};

struct CoverageDatabaseNamespaceExtent {
    std::uint64_t bytes { };
    CoverageDatabaseDigest digest { };

    friend bool operator==(
        const CoverageDatabaseNamespaceExtent&,
        const CoverageDatabaseNamespaceExtent&)
        = default;
};

struct CoverageDatabaseHeader {
    std::array<char, 8> magic { kCoverageDatabaseMagic };
    std::uint32_t schema { kCoverageDatabaseSchema };
    std::uint32_t header_bytes { kCoverageDatabaseHeaderBytes };
    std::uint32_t byte_order_marker { kCoverageDatabaseByteOrderMarker };
    std::uint32_t flags { };
    std::uint64_t container_bytes { kCoverageDatabasePayloadOffset };
    std::uint64_t directory_offset { kCoverageDatabaseDirectoryOffset };
    std::uint32_t directory_entry_bytes {
        kCoverageDatabaseNamespaceDescriptorBytes
    };
    std::uint32_t directory_entries { kCoverageDatabaseNamespaceCount };
    std::uint64_t directory_bytes { kCoverageDatabaseDirectoryBytes };
    std::uint64_t reserved { };

    friend bool operator==(
        const CoverageDatabaseHeader&, const CoverageDatabaseHeader&)
        = default;
};

struct CoverageDatabaseNamespaceDescriptor {
    CoverageDatabaseNamespace kind { CoverageDatabaseNamespace::Code };
    std::uint32_t schema { kCoverageDatabaseNamespaceSchema };
    CoverageDatabaseEncoding encoding { CoverageDatabaseEncoding::Raw };
    std::uint32_t flags { };
    std::uint64_t payload_offset { kCoverageDatabasePayloadOffset };
    std::uint64_t payload_bytes { };
    CoverageDatabaseDigest payload_digest { };

    friend bool operator==(
        const CoverageDatabaseNamespaceDescriptor&,
        const CoverageDatabaseNamespaceDescriptor&)
        = default;
};

struct CoverageDatabaseSchema {
    CoverageDatabaseHeader header;
    std::array<CoverageDatabaseNamespaceDescriptor,
        kCoverageDatabaseNamespaceCount>
        namespaces;

    friend bool operator==(
        const CoverageDatabaseSchema&, const CoverageDatabaseSchema&)
        = default;
};

enum class CoverageDatabaseSchemaError : std::uint8_t {
    None,
    MagicMismatch,
    SchemaMismatch,
    ByteOrderMismatch,
    HeaderMismatch,
    UnsupportedFlags,
    DirectoryMismatch,
    NamespaceMismatch,
    NamespaceSchemaMismatch,
    EncodingMismatch,
    LayoutMismatch,
    ResourceLimit,
    ArithmeticOverflow,
};

struct CoverageDatabaseSchemaResult {
    CoverageDatabaseSchema schema;
    CoverageDatabaseSchemaError error { CoverageDatabaseSchemaError::None };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CoverageDatabaseSchemaError::None;
    }
};

[[nodiscard]] std::string_view coverage_database_namespace_name(
    CoverageDatabaseNamespace value) noexcept;

[[nodiscard]] CoverageDatabaseSchemaResult make_coverage_database_schema(
    const std::array<CoverageDatabaseNamespaceExtent,
        kCoverageDatabaseNamespaceCount>& extents,
    const CoverageDatabaseLimits& limits = { }) noexcept;

[[nodiscard]] CoverageDatabaseSchemaError validate_coverage_database_schema(
    const CoverageDatabaseSchema& schema,
    const CoverageDatabaseLimits& limits = { }) noexcept;

} // namespace fsim::artifact
