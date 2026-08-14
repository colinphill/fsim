// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_effective_archive.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error { std::string { message } };
}

void require_diagnostic(
    const std::vector<fsim::frontend::Diagnostic>& diagnostics,
    const std::string_view code)
{
    require(std::ranges::any_of(diagnostics, [&](const auto& diagnostic) {
        return diagnostic.code == code;
    }),
        "expected cataloged effective-SDF diagnostic");
}

fsim::app::SdfEffectiveArchiveSnapshot snapshot()
{
    using namespace fsim::app;
    SdfEffectiveArchiveSnapshot result;
    result.control_identity = "control:max:two-files";
    result.effective_identity = "effective:generation-9";
    result.generation = 9U;
    result.selection = SdfDelaySelection::Maximum;
    result.policy_identity = "policy:max:preserve-events:preserve-history";
    result.records = {
        { SdfEffectiveValueKind::PathDelay, "top.u0:a->q", "timing/slow.sdf",
            "top", "u*", 0U, 0U, "absolute:max", "sdf40:17:9",
            { 1, 2, 3, -1 }, { 11, 12, 13, -1 } },
        { SdfEffectiveValueKind::TimingCheckLimit, "top.u0:setuphold:0",
            "timing/checks.sdf", "top", "u0", 1U, 2U,
            "increment:max", "sdf40:31:4", { 2, 3 }, { 7, 8 } }
    };
    return result;
}

void test_artifact_cache_checkpoint_round_trips()
{
    using namespace fsim::app;
    constexpr std::array kinds { SdfEffectiveArchiveKind::Object,
        SdfEffectiveArchiveKind::Design, SdfEffectiveArchiveKind::Library,
        SdfEffectiveArchiveKind::NativeCache,
        SdfEffectiveArchiveKind::Checkpoint };
    for (const auto kind : kinds) {
        const auto encoded = encode_sdf_effective_archive(
            snapshot(), kind, "work:sv:top");
        require(encoded.ok() && !encoded.archive_identity.empty(),
            "effective SDF artifact encode must succeed");
        const auto decoded = decode_sdf_effective_archive(encoded.archive,
            kind, "work:sv:top",
            "policy:max:preserve-events:preserve-history");
        require(decoded.ok() && decoded.snapshot == snapshot(),
            "object/design/library/cache/checkpoint round trip must be exact");
        require(decoded.archive_identity == encoded.archive_identity,
            "archive identity must survive a cold decode exactly");

        const auto warm = decode_sdf_effective_archive(encoded.archive, kind,
            "work:sv:top",
            "policy:max:preserve-events:preserve-history");
        require(warm.ok() && warm.archive_identity == decoded.archive_identity,
            "cold and warm cache/replay admission must be identical");
    }
}

void test_order_and_relocation_stability()
{
    using namespace fsim::app;
    auto reordered = snapshot();
    std::ranges::reverse(reordered.records);
    const auto first = encode_sdf_effective_archive(snapshot(),
        SdfEffectiveArchiveKind::Design, "work:sv:top");
    const auto second = encode_sdf_effective_archive(std::move(reordered),
        SdfEffectiveArchiveKind::Design, "work:sv:top");
    require(first.ok() && second.ok() && first.archive == second.archive
            && first.archive_identity == second.archive_identity,
        "canonical effective archives must be input-order independent");

    const auto relocated = decode_sdf_effective_archive(first.archive,
        SdfEffectiveArchiveKind::Design, "work:sv:top",
        "policy:max:preserve-events:preserve-history");
    require(relocated.ok()
            && relocated.snapshot.records.front().source_identity
                == "timing/slow.sdf",
        "relocation must retain producer-relative SDF provenance");
}

void test_stale_corrupt_cross_policy_rejection()
{
    using namespace fsim::app;
    const auto encoded = encode_sdf_effective_archive(snapshot(),
        SdfEffectiveArchiveKind::Checkpoint, "work:sv:top");
    require(encoded.ok(), "negative fixture archive must encode");

    auto stale = encoded.archive;
    stale[8] = std::byte { 2 };
    const auto stale_result = decode_sdf_effective_archive(stale,
        SdfEffectiveArchiveKind::Checkpoint, "work:sv:top",
        "policy:max:preserve-events:preserve-history");
    require(!stale_result.ok(), "superseded schema must be rejected");
    require_diagnostic(stale_result.diagnostics, "FSIM-SDF-EFFECTIVE-002");

    auto corrupt = encoded.archive;
    corrupt.back() ^= std::byte { 1 };
    const auto corrupt_result = decode_sdf_effective_archive(corrupt,
        SdfEffectiveArchiveKind::Checkpoint, "work:sv:top",
        "policy:max:preserve-events:preserve-history");
    require(!corrupt_result.ok(), "checksum corruption must be rejected");
    require_diagnostic(corrupt_result.diagnostics,
        "FSIM-SDF-EFFECTIVE-002");

    const auto wrong_kind = decode_sdf_effective_archive(encoded.archive,
        SdfEffectiveArchiveKind::NativeCache, "work:sv:top",
        "policy:max:preserve-events:preserve-history");
    require(!wrong_kind.ok(), "cross-artifact payload must be rejected");
    require_diagnostic(wrong_kind.diagnostics, "FSIM-SDF-EFFECTIVE-002");

    const auto wrong_producer = decode_sdf_effective_archive(encoded.archive,
        SdfEffectiveArchiveKind::Checkpoint, "work:sv:other",
        "policy:max:preserve-events:preserve-history");
    require(!wrong_producer.ok(), "cross-producer replay must be rejected");
    require_diagnostic(wrong_producer.diagnostics,
        "FSIM-SDF-EFFECTIVE-003");

    const auto wrong_policy = decode_sdf_effective_archive(encoded.archive,
        SdfEffectiveArchiveKind::Checkpoint, "work:sv:top", "policy:min");
    require(!wrong_policy.ok(), "cross-policy replay must be rejected");
    require_diagnostic(wrong_policy.diagnostics, "FSIM-SDF-EFFECTIVE-003");
}

void test_invalid_and_resource_atomicity()
{
    using namespace fsim::app;
    auto duplicate = snapshot();
    duplicate.records.push_back(duplicate.records.front());
    const auto duplicate_result = encode_sdf_effective_archive(
        std::move(duplicate), SdfEffectiveArchiveKind::Object, "work:sv:top");
    require(!duplicate_result.ok(), "duplicate targets must reject");
    require_diagnostic(duplicate_result.diagnostics,
        "FSIM-SDF-EFFECTIVE-003");

    auto absolute = snapshot();
    absolute.records.front().source_identity = "/producer/timing.sdf";
    const auto absolute_result = encode_sdf_effective_archive(
        std::move(absolute), SdfEffectiveArchiveKind::Library, "work:sv:top");
    require(!absolute_result.ok(),
        "producer-absolute provenance must reject before relocation");
    require_diagnostic(absolute_result.diagnostics,
        "FSIM-SDF-EFFECTIVE-001");

    SdfEffectiveArchiveLimits limits;
    limits.max_records = 1U;
    const auto limited = encode_sdf_effective_archive(snapshot(),
        SdfEffectiveArchiveKind::Design, "work:sv:top", limits);
    require(!limited.ok() && limited.archive.empty(),
        "resource failure must not publish a partial archive");
    require_diagnostic(limited.diagnostics, "FSIM-SDF-EFFECTIVE-004");

    const std::array<std::byte, 4> truncated { };
    const auto truncated_result = decode_sdf_effective_archive(truncated,
        SdfEffectiveArchiveKind::Design, "work:sv:top",
        "policy:max:preserve-events:preserve-history");
    require(!truncated_result.ok(), "truncated payload must reject atomically");
    require_diagnostic(truncated_result.diagnostics,
        "FSIM-SDF-EFFECTIVE-002");
}

} // namespace

int main()
{
    try {
        test_artifact_cache_checkpoint_round_trips();
        test_order_and_relocation_stability();
        test_stale_corrupt_cross_policy_rejection();
        test_invalid_and_resource_atomicity();
        std::cout << "effective SDF archive tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
