// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_database.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

fsim::artifact::CoverageDatabaseDigest digest(const std::uint8_t seed)
{
    fsim::artifact::CoverageDatabaseDigest result;
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::byte>(seed + index);
    }
    return result;
}

} // namespace

int main()
{
    using namespace fsim::artifact;

    static_assert(kCoverageDatabaseSchema == 3U);
    static_assert(kCoverageDatabaseNamespaceSchema == 3U);
    static_assert(kCoverageDatabaseHeaderBytes == 64U);
    static_assert(kCoverageDatabaseNamespaceDescriptorBytes == 64U);
    static_assert(kCoverageDatabaseNamespaceCount == 3U);
    static_assert(kCoverageDatabaseDirectoryOffset == 64U);
    static_assert(kCoverageDatabaseDirectoryBytes == 192U);
    static_assert(kCoverageDatabasePayloadOffset == 256U);
    static_assert(kCoverageDatabaseDiagnostic == "FSIM-COV-031");

    assert(coverage_database_namespace_name(CoverageDatabaseNamespace::Code)
        == "code");
    assert(coverage_database_namespace_name(
               CoverageDatabaseNamespace::SystemVerilogFunctional)
        == "systemverilog-functional");
    assert(coverage_database_namespace_name(CoverageDatabaseNamespace::Psl)
        == "psl");
    assert(coverage_database_namespace_name(
        static_cast<CoverageDatabaseNamespace>(0U))
            .empty());

    const std::array<CoverageDatabaseNamespaceExtent,
        kCoverageDatabaseNamespaceCount>
        extents { {
            { 11U, digest(1U) },
            { 17U, digest(2U) },
            { 23U, digest(3U) },
        } };
    const auto made = make_coverage_database_schema(extents);
    assert(made.ok());
    assert(make_coverage_database_schema(extents).schema == made.schema);
    assert(made.schema.header.magic == kCoverageDatabaseMagic);
    assert(made.schema.header.schema == kCoverageDatabaseSchema);
    assert(made.schema.header.container_bytes == 307U);
    assert(made.schema.namespaces[0].kind
        == CoverageDatabaseNamespace::Code);
    assert(made.schema.namespaces[0].payload_offset == 256U);
    assert(made.schema.namespaces[0].payload_bytes == 11U);
    assert(made.schema.namespaces[0].payload_digest == digest(1U));
    assert(made.schema.namespaces[1].kind
        == CoverageDatabaseNamespace::SystemVerilogFunctional);
    assert(made.schema.namespaces[1].payload_offset == 267U);
    assert(made.schema.namespaces[1].payload_bytes == 17U);
    assert(made.schema.namespaces[2].kind
        == CoverageDatabaseNamespace::Psl);
    assert(made.schema.namespaces[2].payload_offset == 284U);
    assert(made.schema.namespaces[2].payload_bytes == 23U);
    assert(validate_coverage_database_schema(made.schema)
        == CoverageDatabaseSchemaError::None);

    const std::array<CoverageDatabaseNamespaceExtent,
        kCoverageDatabaseNamespaceCount>
        empty_extents { };
    const auto empty = make_coverage_database_schema(empty_extents);
    assert(empty.ok());
    assert(empty.schema.header.container_bytes == kCoverageDatabasePayloadOffset);
    for (const auto& descriptor : empty.schema.namespaces) {
        assert(descriptor.payload_offset == kCoverageDatabasePayloadOffset);
        assert(descriptor.payload_bytes == 0U);
    }

    auto invalid = made.schema;
    invalid.header.magic[0] = 'X';
    assert(validate_coverage_database_schema(invalid)
        == CoverageDatabaseSchemaError::MagicMismatch);
    invalid = made.schema;
    invalid.header.schema = 2U;
    assert(validate_coverage_database_schema(invalid)
        == CoverageDatabaseSchemaError::SchemaMismatch);
    invalid = made.schema;
    invalid.header.byte_order_marker = 0x04030201U;
    assert(validate_coverage_database_schema(invalid)
        == CoverageDatabaseSchemaError::ByteOrderMismatch);
    invalid = made.schema;
    invalid.header.header_bytes = 63U;
    assert(validate_coverage_database_schema(invalid)
        == CoverageDatabaseSchemaError::HeaderMismatch);
    invalid = made.schema;
    invalid.header.flags = 1U;
    assert(validate_coverage_database_schema(invalid)
        == CoverageDatabaseSchemaError::UnsupportedFlags);
    invalid = made.schema;
    invalid.header.reserved = 1U;
    assert(validate_coverage_database_schema(invalid)
        == CoverageDatabaseSchemaError::UnsupportedFlags);
    invalid = made.schema;
    invalid.header.directory_entries = 2U;
    assert(validate_coverage_database_schema(invalid)
        == CoverageDatabaseSchemaError::DirectoryMismatch);
    invalid = made.schema;
    invalid.namespaces[0].kind = CoverageDatabaseNamespace::Psl;
    assert(validate_coverage_database_schema(invalid)
        == CoverageDatabaseSchemaError::NamespaceMismatch);
    invalid = made.schema;
    invalid.namespaces[1].schema = 2U;
    assert(validate_coverage_database_schema(invalid)
        == CoverageDatabaseSchemaError::NamespaceSchemaMismatch);
    invalid = made.schema;
    invalid.namespaces[1].encoding
        = static_cast<CoverageDatabaseEncoding>(1U);
    assert(validate_coverage_database_schema(invalid)
        == CoverageDatabaseSchemaError::EncodingMismatch);
    invalid = made.schema;
    invalid.namespaces[1].flags = 1U;
    assert(validate_coverage_database_schema(invalid)
        == CoverageDatabaseSchemaError::UnsupportedFlags);
    invalid = made.schema;
    ++invalid.namespaces[1].payload_offset;
    assert(validate_coverage_database_schema(invalid)
        == CoverageDatabaseSchemaError::LayoutMismatch);
    invalid = made.schema;
    ++invalid.header.container_bytes;
    assert(validate_coverage_database_schema(invalid)
        == CoverageDatabaseSchemaError::LayoutMismatch);

    auto limits = CoverageDatabaseLimits { };
    limits.maximum_namespaces = 2U;
    assert(make_coverage_database_schema(extents, limits).error
        == CoverageDatabaseSchemaError::ResourceLimit);
    limits = { };
    limits.maximum_directory_bytes = kCoverageDatabaseDirectoryBytes - 1U;
    assert(make_coverage_database_schema(extents, limits).error
        == CoverageDatabaseSchemaError::ResourceLimit);
    limits = { };
    limits.maximum_container_bytes = kCoverageDatabasePayloadOffset - 1U;
    assert(make_coverage_database_schema(extents, limits).error
        == CoverageDatabaseSchemaError::ResourceLimit);
    limits = { };
    limits.maximum_namespace_bytes = 16U;
    assert(make_coverage_database_schema(extents, limits).error
        == CoverageDatabaseSchemaError::ResourceLimit);
    limits = { };
    limits.maximum_container_bytes = 306U;
    assert(make_coverage_database_schema(extents, limits).error
        == CoverageDatabaseSchemaError::ResourceLimit);

    auto excessive = empty_extents;
    excessive[0].bytes = std::numeric_limits<std::uint64_t>::max();
    limits.maximum_container_bytes
        = std::numeric_limits<std::uint64_t>::max();
    limits.maximum_namespace_bytes
        = std::numeric_limits<std::uint64_t>::max();
    assert(make_coverage_database_schema(excessive, limits).error
        == CoverageDatabaseSchemaError::ArithmeticOverflow);
}
