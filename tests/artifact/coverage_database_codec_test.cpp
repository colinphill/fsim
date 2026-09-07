// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_database_codec.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <ranges>
#include <string>

namespace {

using namespace fsim::artifact;

CoverageDatabaseIdentity id(const std::uint64_t value)
{
    return { value, value * 29U };
}

CoverageDatabaseDigest digest(const std::uint8_t seed)
{
    CoverageDatabaseDigest result;
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::byte>(seed + index);
    }
    return result;
}

CoverageDatabaseContents example()
{
    CoverageDatabaseContents contents;
    contents.fingerprint.digest = digest(60U);
    contents.sources = {
        { id(2U), "rtl/properties.psl", 200U, digest(2U) },
        { id(1U), "rtl/design.sv", 100U, digest(1U) },
    };
    contents.runs = {
        { id(4U), "second", "fsim 3.0.0-dev", 8U, 80U, 2U,
            CoverageDatabaseRunStatus::Stopped },
        { id(3U), "first", "fsim 3.0.0-dev", 7U, 70U, 1U,
            CoverageDatabaseRunStatus::Complete },
    };
    contents.metrics = {
        { CoverageDatabaseNamespace::Psl,
            CoverageDatabaseMetricFamily::PslProperty,
            CoverageDatabaseMetricScope::Instance, id(13U), id(2U), id(23U),
            id(4U), std::numeric_limits<std::uint64_t>::max(), 0U, true,
            false, 31U },
        { CoverageDatabaseNamespace::SystemVerilogFunctional,
            CoverageDatabaseMetricFamily::SystemVerilogCross,
            CoverageDatabaseMetricScope::Instance, id(12U), id(1U), id(22U),
            id(3U), 5U, 2U, false, false, 21U },
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement,
            CoverageDatabaseMetricScope::Source, id(11U), id(1U), { }, id(3U),
            1U, 0U, false, false, 12U },
    };
    contents.exclusions = {
        { CoverageDatabaseNamespace::SystemVerilogFunctional,
            CoverageDatabaseMetricFamily::SystemVerilogCross,
            CoverageDatabaseMetricScope::Instance, id(12U), id(1U), id(22U),
            "excluded cross", 21U },
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Branch,
            CoverageDatabaseMetricScope::Source, id(14U), id(1U), { },
            "unreachable", 13U },
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Branch,
            CoverageDatabaseMetricScope::Source, id(14U), id(1U), { },
            "waived by plan", 13U },
    };
    return contents;
}

std::filesystem::path temporary_directory()
{
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    return std::filesystem::temp_directory_path()
        / ("fsim-coverage-codec-" + std::to_string(nonce));
}

} // namespace

int main()
{
    static_assert(kCoverageDatabaseCodecDiagnostic == "FSIM-COV-035");
    const auto canonical = make_coverage_database_contents(example());
    assert(canonical.ok());
    const auto first = serialize_coverage_database(example());
    const auto second = serialize_coverage_database(example());
    assert(first.ok() && second.ok() && first.bytes == second.bytes
        && first.bytes.size() > kCoverageDatabasePayloadOffset);
    assert(std::ranges::equal(std::span { first.bytes }.first(8U),
        std::as_bytes(std::span {
            kCoverageDatabaseMagic.data(), kCoverageDatabaseMagic.size() })));
    const auto decoded = deserialize_coverage_database(first.bytes);
    assert(decoded.ok() && decoded.contents == canonical.contents);

    auto invalid = first.bytes;
    invalid[11] = std::byte { 2U };
    auto rejected = deserialize_coverage_database(invalid);
    assert(rejected.error == CoverageDatabaseCodecError::InvalidSchema
        && rejected.schema_error == CoverageDatabaseSchemaError::SchemaMismatch);
    invalid = first.bytes;
    invalid[71] = std::byte { 2U };
    rejected = deserialize_coverage_database(invalid);
    assert(rejected.error == CoverageDatabaseCodecError::InvalidSchema
        && rejected.schema_error
            == CoverageDatabaseSchemaError::NamespaceSchemaMismatch);
    invalid = first.bytes;
    invalid[kCoverageDatabasePayloadOffset + 1U] ^= std::byte { 1U };
    assert(deserialize_coverage_database(invalid).error
        == CoverageDatabaseCodecError::DigestMismatch);
    invalid = first.bytes;
    invalid.pop_back();
    assert(deserialize_coverage_database(invalid).error
        == CoverageDatabaseCodecError::Malformed);

    auto limits = CoverageDatabaseCodecLimits { };
    limits.container.maximum_namespace_bytes = 64U;
    assert(serialize_coverage_database(example(), limits).error
        == CoverageDatabaseCodecError::ResourceLimit);
    limits = { };
    limits.model.maximum_metrics = 2U;
    assert(serialize_coverage_database(example(), limits).error
        == CoverageDatabaseCodecError::InvalidModel);
    assert(deserialize_coverage_database(first.bytes, limits).error
        == CoverageDatabaseCodecError::ResourceLimit);

    const auto directory = temporary_directory();
    const auto path = directory / "results.fsimcov";
    assert(write_coverage_database_atomically(path, example()).ok());
    assert(read_coverage_database(path).contents == canonical.contents);
    auto replacement = example();
    replacement.metrics[0].hits = 9U;
    replacement.metrics[0].overflow = false;
    const auto replacement_canonical
        = make_coverage_database_contents(replacement);
    assert(replacement_canonical.ok());
    assert(write_coverage_database_atomically(path, replacement).ok());
    assert(read_coverage_database(path).contents
        == replacement_canonical.contents);
    assert(!std::filesystem::exists(
               std::filesystem::path { path.string() + ".fsim-tmp" })
        && !std::filesystem::exists(
            std::filesystem::path { path.string() + ".fsim-old" }));
    const auto backup
        = std::filesystem::path { path.string() + ".fsim-old" };
    std::filesystem::rename(path, backup);
    assert(write_coverage_database_atomically(path, example()).ok());
    assert(read_coverage_database(path).contents == canonical.contents
        && !std::filesystem::exists(backup));
    std::filesystem::remove_all(directory);

    assert(write_coverage_database_atomically({ }, example()).error
        == CoverageDatabaseCodecError::IoFailure);
    assert(read_coverage_database(directory / "missing.fsimcov").error
        == CoverageDatabaseCodecError::IoFailure);
}
