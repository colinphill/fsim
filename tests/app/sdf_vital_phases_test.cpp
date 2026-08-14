// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_vital_phases.hpp"

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
        app::SdfVitalTimingStatePolicy::ResetHistory, "vital-phase-source");
}

std::shared_ptr<const fsim::app::SdfForeignInterfaceApplication> foreign()
{
    fsim::app::SdfForeignTimingObject object;
    object.interface_kind = fsim::app::SdfForeignInterfaceKind::Vhpi;
    object.timing_kind = fsim::app::SdfForeignTimingKind::VitalDelay;
    object.handle = 1U;
    object.generation = 31U;
    object.root_identity = "root-a";
    object.library_identity = "work";
    object.hierarchy_path = "top.u";
    object.source_identity = "vital-delay";
    object.effective_ticks = { 7U, 9U };
    object.canonical_identity = "foreign-vital-delay";
    return std::make_shared<const fsim::app::SdfForeignInterfaceApplication>(
        std::vector<fsim::app::SdfForeignTimingObject> { std::move(object) },
        "foreign-phase-source");
}

std::shared_ptr<const fsim::app::SdfVitalArchiveApplication> archive()
{
    const auto built = fsim::app::build_sdf_vital_archive(vital(), foreign());
    require(built.ok(), "phase fixture archive must build");
    return built.application;
}

void test_surface_phase_and_engine_equivalence()
{
    using namespace fsim::app;
    constexpr std::array surfaces {
        SdfVitalPhaseSurface::Project,
        SdfVitalPhaseSurface::CommandLine,
        SdfVitalPhaseSurface::Tcl,
        SdfVitalPhaseSurface::CApi,
        SdfVitalPhaseSurface::CppApi,
        SdfVitalPhaseSurface::NonProject,
    };
    constexpr std::array phases {
        SdfVitalExecutionPhase::Cold,
        SdfVitalExecutionPhase::Warm,
        SdfVitalExecutionPhase::Relocated,
    };
    constexpr std::array engines {
        SdfVitalPhaseEngine::Interpreter,
        SdfVitalPhaseEngine::Llvm,
    };
    const auto input = archive();
    std::string behavior_identity;
    std::string archive_identity;
    std::vector<std::byte> encoded_reference;
    std::size_t executions { };
    for (const auto surface : surfaces) {
        for (const auto phase : phases) {
            for (const auto engine : engines) {
                SdfVitalPhaseControl control;
                control.surface = surface;
                control.phase = phase;
                control.engine = engine;
                control.artifact_kind = SdfEffectiveArchiveKind::Library;
                control.producer_identity = "logical:work.top";
                const auto built
                    = build_sdf_vital_phase(input, std::move(control));
                require(built.ok(),
                    "every project/direct surface, phase and engine must build");
                const auto& summary = built.application->summary();
                require(summary.generation == 31U
                        && summary.record_count == 1U
                        && summary.control.surface == surface
                        && summary.control.phase == phase
                        && summary.control.engine == engine
                        && summary.selection == SdfDelaySelection::Typical
                        && !summary.semantic_identity.empty(),
                    "each surface must report exact effective timing controls");
                const auto bytes = built.application->encoded_archive();
                if (executions == 0U) {
                    behavior_identity = summary.behavior_identity;
                    archive_identity = summary.archive_identity;
                    encoded_reference.assign(bytes.begin(), bytes.end());
                } else {
                    require(summary.behavior_identity == behavior_identity
                            && summary.archive_identity == archive_identity
                            && std::vector<std::byte>(bytes.begin(), bytes.end())
                                == encoded_reference,
                        "surfaces, phases and engines must retain identical behavior and archive bytes");
                }
                ++executions;
            }
        }
    }
    require(executions == 36U,
        "project/CLI/Tcl/C/C++/non-project phase matrix must be complete");
}

void test_equivalent_failures_and_resource_rollback()
{
    using namespace fsim::app;
    constexpr std::array surfaces {
        SdfVitalPhaseSurface::Project,
        SdfVitalPhaseSurface::CommandLine,
        SdfVitalPhaseSurface::Tcl,
        SdfVitalPhaseSurface::CApi,
        SdfVitalPhaseSurface::CppApi,
        SdfVitalPhaseSurface::NonProject,
    };
    const auto input = archive();
    std::string reference_code;
    std::string reference_message;
    for (const auto surface : surfaces) {
        SdfVitalPhaseControl invalid;
        invalid.surface = surface;
        invalid.phase = static_cast<SdfVitalExecutionPhase>(255U);
        invalid.producer_identity = "logical:work.top";
        const auto rejected = build_sdf_vital_phase(input, std::move(invalid));
        require(!rejected.ok() && rejected.application == nullptr
                && rejected.diagnostics.size() == 1U
                && rejected.diagnostics.front().code
                    == "FSIM-SDF-VITAL-PHASE-002",
            "unsupported phase must reject atomically at every surface");
        if (reference_code.empty()) {
            reference_code = rejected.diagnostics.front().code;
            reference_message = rejected.diagnostics.front().message;
        } else {
            require(rejected.diagnostics.front().code == reference_code
                    && rejected.diagnostics.front().message
                        == reference_message,
                "project/CLI/Tcl/C/C++/non-project failures must be identical");
        }
    }
    SdfVitalPhaseControl missing_producer;
    const auto incomplete
        = build_sdf_vital_phase(input, std::move(missing_producer));
    require(!incomplete.ok()
            && incomplete.diagnostics.front().code
                == "FSIM-SDF-VITAL-PHASE-001",
        "incomplete control must reject without partial summary");
    SdfVitalPhaseControl bounded_control;
    bounded_control.producer_identity = "logical:work.top";
    SdfVitalPhaseLimits limits;
    limits.archive.max_archive_bytes = 1U;
    const auto archive_exhausted
        = build_sdf_vital_phase(input, bounded_control, limits);
    require(!archive_exhausted.ok()
            && archive_exhausted.diagnostics.front().code
                == "FSIM-SDF-VITAL-PHASE-004",
        "archive resource exhaustion must translate and roll back");
    limits = { };
    limits.max_summary_identity_bytes = 1U;
    const auto summary_exhausted
        = build_sdf_vital_phase(input, std::move(bounded_control), limits);
    require(!summary_exhausted.ok()
            && summary_exhausted.diagnostics.front().code
                == "FSIM-SDF-VITAL-PHASE-004",
        "summary identity exhaustion must roll back atomically");
}
} // namespace

int main()
{
    try {
        test_surface_phase_and_engine_equivalence();
        test_equivalent_failures_and_resource_rollback();
    } catch (const std::exception& error) {
        std::cerr << "sdf vital phases test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "sdf vital phases tests passed\n";
    return 0;
}
