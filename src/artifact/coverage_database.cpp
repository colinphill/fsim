// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_database.hpp"

#include <limits>

namespace fsim::artifact {
namespace {

    inline constexpr std::array<CoverageDatabaseNamespace,
        kCoverageDatabaseNamespaceCount>
        kNamespaces {
            CoverageDatabaseNamespace::Code,
            CoverageDatabaseNamespace::SystemVerilogFunctional,
            CoverageDatabaseNamespace::Psl,
        };

    bool checked_add(
        const std::uint64_t left,
        const std::uint64_t right,
        std::uint64_t& result) noexcept
    {
        if (right > std::numeric_limits<std::uint64_t>::max() - left) {
            return false;
        }
        result = left + right;
        return true;
    }

    bool limits_admit_schema(const CoverageDatabaseLimits& limits) noexcept
    {
        return limits.maximum_namespaces >= kCoverageDatabaseNamespaceCount
            && limits.maximum_directory_bytes >= kCoverageDatabaseDirectoryBytes
            && limits.maximum_container_bytes >= kCoverageDatabasePayloadOffset;
    }

    CoverageDatabaseSchemaResult failure(
        const CoverageDatabaseSchemaError error) noexcept
    {
        return { { }, error };
    }

} // namespace

std::string_view coverage_database_namespace_name(
    const CoverageDatabaseNamespace value) noexcept
{
    switch (value) {
    case CoverageDatabaseNamespace::Code:
        return "code";
    case CoverageDatabaseNamespace::SystemVerilogFunctional:
        return "systemverilog-functional";
    case CoverageDatabaseNamespace::Psl:
        return "psl";
    }
    return { };
}

CoverageDatabaseSchemaResult make_coverage_database_schema(
    const std::array<CoverageDatabaseNamespaceExtent,
        kCoverageDatabaseNamespaceCount>& extents,
    const CoverageDatabaseLimits& limits) noexcept
{
    if (!limits_admit_schema(limits)) {
        return failure(CoverageDatabaseSchemaError::ResourceLimit);
    }

    CoverageDatabaseSchema result;
    std::uint64_t cursor = kCoverageDatabasePayloadOffset;
    for (std::size_t index = 0; index < result.namespaces.size(); ++index) {
        const auto& extent = extents[index];
        if (extent.bytes > limits.maximum_namespace_bytes) {
            return failure(CoverageDatabaseSchemaError::ResourceLimit);
        }
        std::uint64_t next { };
        if (!checked_add(cursor, extent.bytes, next)) {
            return failure(CoverageDatabaseSchemaError::ArithmeticOverflow);
        }
        if (next > limits.maximum_container_bytes) {
            return failure(CoverageDatabaseSchemaError::ResourceLimit);
        }
        auto& descriptor = result.namespaces[index];
        descriptor.kind = kNamespaces[index];
        descriptor.payload_offset = cursor;
        descriptor.payload_bytes = extent.bytes;
        descriptor.payload_digest = extent.digest;
        cursor = next;
    }
    result.header.container_bytes = cursor;

    const auto error = validate_coverage_database_schema(result, limits);
    if (error != CoverageDatabaseSchemaError::None) {
        return failure(error);
    }
    return { result, CoverageDatabaseSchemaError::None };
}

CoverageDatabaseSchemaError validate_coverage_database_schema(
    const CoverageDatabaseSchema& schema,
    const CoverageDatabaseLimits& limits) noexcept
{
    if (schema.header.magic != kCoverageDatabaseMagic) {
        return CoverageDatabaseSchemaError::MagicMismatch;
    }
    if (schema.header.schema != kCoverageDatabaseSchema) {
        return CoverageDatabaseSchemaError::SchemaMismatch;
    }
    if (schema.header.byte_order_marker != kCoverageDatabaseByteOrderMarker) {
        return CoverageDatabaseSchemaError::ByteOrderMismatch;
    }
    if (schema.header.header_bytes != kCoverageDatabaseHeaderBytes) {
        return CoverageDatabaseSchemaError::HeaderMismatch;
    }
    if (schema.header.flags != 0U || schema.header.reserved != 0U) {
        return CoverageDatabaseSchemaError::UnsupportedFlags;
    }
    if (schema.header.directory_offset != kCoverageDatabaseDirectoryOffset
        || schema.header.directory_entry_bytes
            != kCoverageDatabaseNamespaceDescriptorBytes
        || schema.header.directory_entries != kCoverageDatabaseNamespaceCount
        || schema.header.directory_bytes != kCoverageDatabaseDirectoryBytes) {
        return CoverageDatabaseSchemaError::DirectoryMismatch;
    }
    if (!limits_admit_schema(limits)
        || schema.header.container_bytes > limits.maximum_container_bytes) {
        return CoverageDatabaseSchemaError::ResourceLimit;
    }

    std::uint64_t cursor = kCoverageDatabasePayloadOffset;
    for (std::size_t index = 0; index < schema.namespaces.size(); ++index) {
        const auto& descriptor = schema.namespaces[index];
        if (descriptor.kind != kNamespaces[index]
            || coverage_database_namespace_name(descriptor.kind).empty()) {
            return CoverageDatabaseSchemaError::NamespaceMismatch;
        }
        if (descriptor.schema != kCoverageDatabaseNamespaceSchema) {
            return CoverageDatabaseSchemaError::NamespaceSchemaMismatch;
        }
        if (descriptor.encoding != CoverageDatabaseEncoding::Raw) {
            return CoverageDatabaseSchemaError::EncodingMismatch;
        }
        if (descriptor.flags != 0U) {
            return CoverageDatabaseSchemaError::UnsupportedFlags;
        }
        if (descriptor.payload_offset != cursor) {
            return CoverageDatabaseSchemaError::LayoutMismatch;
        }
        if (descriptor.payload_bytes > limits.maximum_namespace_bytes) {
            return CoverageDatabaseSchemaError::ResourceLimit;
        }
        std::uint64_t next { };
        if (!checked_add(cursor, descriptor.payload_bytes, next)) {
            return CoverageDatabaseSchemaError::ArithmeticOverflow;
        }
        if (next > limits.maximum_container_bytes) {
            return CoverageDatabaseSchemaError::ResourceLimit;
        }
        cursor = next;
    }
    if (schema.header.container_bytes != cursor) {
        return CoverageDatabaseSchemaError::LayoutMismatch;
    }
    return CoverageDatabaseSchemaError::None;
}

} // namespace fsim::artifact
