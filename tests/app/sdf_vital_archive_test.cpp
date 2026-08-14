// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_vital_archive.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string { message });
}

std::shared_ptr<const fsim::app::SdfVitalReannotationApplication> vital()
{
    using namespace fsim;
    app::SdfVitalScheduledDelay delay;
    delay.call.canonical_identity = "work.top.delay";
    delay.source_delay_ticks = { 2U, 4U };
    delay.effective_delay_ticks = { 7U, 9U };
    delay.canonical_identity = "vital-delay";
    auto scheduling
        = std::make_shared<const app::SdfVitalSchedulingApplication>(nullptr,
            elaboration::ElaboratedDesign { },
            std::vector<app::SdfVitalScheduledDelay> { delay }, "scheduling");
    auto baseline
        = std::make_shared<const app::SdfVitalTimingCheckApplication>(scheduling,
            elaboration::ElaboratedDesign { },
            std::vector<app::SdfVitalScheduledTimingCheck> { }, "timing");
    return std::make_shared<const app::SdfVitalReannotationApplication>(
        baseline, elaboration::ElaboratedDesign { },
        std::vector<app::SdfVitalScheduledDelay> { std::move(delay) },
        std::vector<app::SdfVitalScheduledTimingCheck> { },
        std::vector<app::SdfVitalTimingGenericValue> { },
        std::vector<runtime::simir::Interpreter::VitalTimingReannotation> { },
        std::vector<app::SdfVitalReannotationRevision> { }, 31U,
        app::SdfVitalPendingTransactionPolicy::PreserveScheduledTiming,
        app::SdfVitalTimingStatePolicy::ResetHistory, "vital-archive-source");
}

std::shared_ptr<const fsim::app::SdfForeignInterfaceApplication> foreign(
    const bool stale = false)
{
    fsim::app::SdfForeignTimingObject object;
    object.interface_kind = fsim::app::SdfForeignInterfaceKind::Vhpi;
    object.timing_kind = fsim::app::SdfForeignTimingKind::VitalDelay;
    object.handle = 1U;
    object.generation = 31U;
    object.root_identity = "root-a";
    object.library_identity = "work";
    object.hierarchy_path = "top.u";
    object.source_identity = stale ? std::string { } : "vital-delay";
    object.effective_ticks = { 7U, 9U };
    object.canonical_identity = "foreign-vital-delay";
    return std::make_shared<const fsim::app::SdfForeignInterfaceApplication>(
        std::vector<fsim::app::SdfForeignTimingObject> { std::move(object) },
        "foreign-archive-source");
}

void test_all_artifact_kinds_and_relocation()
{
    using namespace fsim;
    const auto built = app::build_sdf_vital_archive(vital(), foreign());
    require(built.ok() && built.application->snapshot().records.size() == 1U,
        "VITAL archive snapshot must retain one effective timing record");
    const auto& snapshot = built.application->snapshot();
    require(snapshot.records.front().original_values
                == std::vector<std::int64_t> { 2, 4 }
            && snapshot.records.front().effective_values
                == std::vector<std::int64_t> { 7, 9 }
            && !snapshot.policy_identity.empty(),
        "source/effective values and timing-state policy must remain exact");
    constexpr std::array kinds {
        app::SdfEffectiveArchiveKind::Object,
        app::SdfEffectiveArchiveKind::Design,
        app::SdfEffectiveArchiveKind::Library,
        app::SdfEffectiveArchiveKind::NativeCache,
        app::SdfEffectiveArchiveKind::Checkpoint,
    };
    for (const auto kind : kinds) {
        const auto encoded = app::encode_sdf_vital_archive(
            *built.application, kind, "logical:work.top");
        require(encoded.ok(), "each governed artifact kind must encode");
        const auto decoded = app::decode_sdf_vital_archive(encoded.archive,
            kind, "logical:work.top", snapshot.policy_identity);
        require(decoded.ok() && decoded.snapshot == snapshot,
            "object/design/library/cache/checkpoint must round-trip exactly");
    }
    const auto first = app::encode_sdf_vital_archive(*built.application,
        app::SdfEffectiveArchiveKind::Library, "logical:work.top");
    const auto relocated = app::encode_sdf_vital_archive(*built.application,
        app::SdfEffectiveArchiveKind::Library, "logical:work.top");
    require(first.archive == relocated.archive
            && first.archive_identity == relocated.archive_identity,
        "physical relocation must not change logical mapped-library identity");
}

void test_stale_corrupt_incompatible_and_resource_rejection()
{
    using namespace fsim;
    auto built = app::build_sdf_vital_archive(nullptr, foreign());
    require(!built.ok() && built.diagnostics.front().code == "FSIM-SDF-VITAL-ARCHIVE-001",
        "missing effective application must reject atomically");
    built = app::build_sdf_vital_archive(vital(), foreign(true));
    require(!built.ok() && built.diagnostics.front().code == "FSIM-SDF-VITAL-ARCHIVE-003",
        "stale source identity must reject atomically");
    app::SdfEffectiveArchiveLimits limits;
    limits.max_records = 0U;
    built = app::build_sdf_vital_archive(vital(), foreign(), limits);
    require(!built.ok() && built.diagnostics.front().code == "FSIM-SDF-VITAL-ARCHIVE-001",
        "zero archive resources must reject atomically");

    built = app::build_sdf_vital_archive(vital(), foreign());
    auto encoded = app::encode_sdf_vital_archive(*built.application,
        app::SdfEffectiveArchiveKind::Checkpoint, "logical:work.top");
    auto corrupt = encoded.archive;
    corrupt.back() ^= std::byte { 1U };
    auto decoded = app::decode_sdf_vital_archive(corrupt,
        app::SdfEffectiveArchiveKind::Checkpoint, "logical:work.top",
        built.application->snapshot().policy_identity);
    require(!decoded.ok() && decoded.diagnostics.front().code == "FSIM-SDF-VITAL-ARCHIVE-002",
        "corrupt checkpoint payload must reject transactionally");
    decoded = app::decode_sdf_vital_archive(encoded.archive,
        app::SdfEffectiveArchiveKind::Design, "logical:work.top",
        built.application->snapshot().policy_identity);
    require(!decoded.ok() && decoded.diagnostics.front().code == "FSIM-SDF-VITAL-ARCHIVE-002",
        "cross-kind stale artifact must reject transactionally");
}
} // namespace

int main()
{
    try {
        test_all_artifact_kinds_and_relocation();
        test_stale_corrupt_incompatible_and_resource_rejection();
    } catch (const std::exception& error) {
        std::cerr << "sdf vital archive test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "sdf vital archive tests passed\n";
    return 0;
}
