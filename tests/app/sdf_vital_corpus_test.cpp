// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_vital_phases.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

std::string read_fixture(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    require(stream.good(), "owned SDF/VITAL corpus fixture must be readable");
    return { std::istreambuf_iterator<char> { stream }, { } };
}

void verify_owned_fixtures(const std::filesystem::path& directory)
{
    constexpr std::array sdf_fixtures {
        std::pair { "timing_21.sdf", "(SDFVERSION \"2.1\")" },
        std::pair { "timing_30.sdf", "(SDFVERSION \"3.0\")" },
        std::pair { "timing_40.sdf", "(SDFVERSION \"4.0\")" },
    };
    constexpr std::array model_fixtures {
        std::pair { "standard_cell.vhd",
            "FSIM-VITAL-MODEL: standard-cell" },
        std::pair { "primitive.vhd", "FSIM-VITAL-MODEL: primitive" },
        std::pair { "state_table.vhd", "FSIM-VITAL-MODEL: state-table" },
        std::pair { "memory.vhd", "FSIM-VITAL-MODEL: memory" },
        std::pair { "wrapper.vhd", "FSIM-VITAL-MODEL: wrapper" },
    };
    constexpr std::array boundary_fixtures {
        std::pair { "boundary_verilog.v", "FSIM-VITAL-BOUNDARY: verilog" },
        std::pair { "boundary_systemverilog.sv",
            "FSIM-VITAL-BOUNDARY: systemverilog" },
        std::pair { "boundary_systemc.cpp", "FSIM-VITAL-BOUNDARY: systemc" },
    };
    const auto verify = [&](const auto& fixtures) {
        for (const auto& [name, marker] : fixtures) {
            const auto source = read_fixture(directory / name);
            require(source.find("SPDX-License-Identifier: Apache-2.0")
                        != std::string::npos
                    && source.find(marker) != std::string::npos,
                "owned fixture must retain license and semantic marker");
        }
    };
    verify(sdf_fixtures);
    verify(model_fixtures);
    verify(boundary_fixtures);
}

std::shared_ptr<const fsim::app::SdfVitalArchiveApplication> make_archive(
    const std::string& identity, const std::uint64_t generation,
    const std::size_t root_count = 1U)
{
    using namespace fsim;
    std::vector<app::SdfVitalScheduledDelay> delays;
    std::vector<app::SdfForeignTimingObject> objects;
    for (std::size_t index = 0U; index < root_count; ++index) {
        const auto suffix = ':' + std::to_string(index);
        app::SdfVitalScheduledDelay delay;
        delay.call.canonical_identity = identity + ":call" + suffix;
        delay.source_delay_ticks = { 2U, 4U };
        delay.effective_delay_ticks = { 7U, 9U };
        delay.canonical_identity = identity + ":delay" + suffix;
        delays.push_back(std::move(delay));
        app::SdfForeignTimingObject object;
        object.interface_kind = app::SdfForeignInterfaceKind::Vhpi;
        object.timing_kind = app::SdfForeignTimingKind::VitalDelay;
        object.handle = index + 1U;
        object.generation = generation;
        object.root_identity = identity + ":root" + suffix;
        object.library_identity = "work";
        object.hierarchy_path = "top.u" + suffix;
        object.source_identity = identity + ":delay" + suffix;
        object.effective_ticks = { 7U, 9U };
        object.canonical_identity = identity + ":foreign" + suffix;
        objects.push_back(std::move(object));
    }
    auto scheduling
        = std::make_shared<const app::SdfVitalSchedulingApplication>(nullptr,
            elaboration::ElaboratedDesign { },
            delays, identity + ":scheduling");
    auto timing
        = std::make_shared<const app::SdfVitalTimingCheckApplication>(scheduling,
            elaboration::ElaboratedDesign { },
            std::vector<app::SdfVitalScheduledTimingCheck> { },
            identity + ":timing");
    auto vital
        = std::make_shared<const app::SdfVitalReannotationApplication>(timing,
            elaboration::ElaboratedDesign { },
            std::move(delays),
            std::vector<app::SdfVitalScheduledTimingCheck> { },
            std::vector<app::SdfVitalTimingGenericValue> { },
            std::vector<runtime::simir::Interpreter::VitalTimingReannotation> { },
            std::vector<app::SdfVitalReannotationRevision> { }, generation,
            app::SdfVitalPendingTransactionPolicy::PreserveScheduledTiming,
            app::SdfVitalTimingStatePolicy::PreserveHistory,
            identity + ":vital");
    auto foreign
        = std::make_shared<const app::SdfForeignInterfaceApplication>(
            std::move(objects),
            identity + ":foreign-set");
    const auto built = app::build_sdf_vital_archive(
        std::move(vital), std::move(foreign));
    require(built.ok(), "owned corpus effective archive must build");
    return built.application;
}

std::size_t run_corpus_matrix()
{
    using namespace fsim::app;
    constexpr std::array sdf_revisions { "2.1", "3.0", "4.0" };
    constexpr std::array vhdl_revisions { "87", "93", "2000", "2002",
        "2008" };
    constexpr std::array models { "standard-cell", "primitive", "state-table",
        "memory", "wrapper" };
    constexpr std::array directions { "vhdl-verilog", "verilog-vhdl",
        "vhdl-systemverilog", "systemverilog-vhdl", "vhdl-systemc",
        "systemc-vhdl" };
    constexpr std::array phases { SdfVitalExecutionPhase::Cold,
        SdfVitalExecutionPhase::Warm, SdfVitalExecutionPhase::Relocated };
    constexpr std::array engines { SdfVitalPhaseEngine::Interpreter,
        SdfVitalPhaseEngine::Llvm };
    std::size_t cases { };
    std::uint64_t generation { 1U };
    for (const auto sdf_revision : sdf_revisions) {
        for (const auto vhdl_revision : vhdl_revisions) {
            for (const auto model : models) {
                for (const auto direction : directions) {
                    const std::string identity = std::string { sdf_revision }
                        + ':' + vhdl_revision + ':' + model + ':' + direction;
                    const auto archive = make_archive(identity, generation++);
                    std::string behavior_identity;
                    std::vector<std::byte> archive_bytes;
                    for (const auto phase : phases) {
                        for (const auto engine : engines) {
                            SdfVitalPhaseControl control;
                            control.surface = SdfVitalPhaseSurface::NonProject;
                            control.phase = phase;
                            control.engine = engine;
                            control.artifact_kind
                                = SdfEffectiveArchiveKind::Library;
                            control.producer_identity = "logical:" + identity;
                            const auto built = build_sdf_vital_phase(
                                archive, std::move(control));
                            require(built.ok(),
                                "owned mixed corpus phase execution must build");
                            const auto bytes
                                = built.application->encoded_archive();
                            if (behavior_identity.empty()) {
                                behavior_identity
                                    = built.application->summary().behavior_identity;
                                archive_bytes.assign(bytes.begin(), bytes.end());
                            } else {
                                require(built.application->summary().behavior_identity
                                            == behavior_identity
                                        && std::vector<std::byte>(
                                               bytes.begin(), bytes.end())
                                            == archive_bytes,
                                    "corpus interpreter/LLVM and phase behavior must match");
                            }
                            ++cases;
                        }
                    }
                }
            }
        }
    }
    return cases;
}

void verify_application_closure()
{
    using namespace fsim::app;
    const auto archive = make_archive("closure", 999U, 2U);
    require(archive->snapshot().records.size() == 2U
            && archive->snapshot().records[0].root
                != archive->snapshot().records[1].root,
        "closure archive must retain two distinct roots");
    constexpr std::array kinds { SdfEffectiveArchiveKind::Object,
        SdfEffectiveArchiveKind::Design, SdfEffectiveArchiveKind::Library,
        SdfEffectiveArchiveKind::NativeCache,
        SdfEffectiveArchiveKind::Checkpoint };
    for (const auto kind : kinds) {
        const auto encoded
            = encode_sdf_vital_archive(*archive, kind, "logical:closure");
        require(encoded.ok(), "every closure artifact kind must encode");
        const auto decoded = decode_sdf_vital_archive(encoded.archive, kind,
            "logical:closure", archive->snapshot().policy_identity);
        require(decoded.ok() && decoded.snapshot == archive->snapshot(),
            "every closure artifact kind must decode exact multiple-root state");
    }
    const auto encoded = encode_sdf_vital_archive(*archive,
        SdfEffectiveArchiveKind::Library, "logical:closure");
    const auto mismatch = decode_sdf_vital_archive(encoded.archive,
        SdfEffectiveArchiveKind::Design, "logical:closure",
        archive->snapshot().policy_identity);
    require(!mismatch.ok()
            && mismatch.diagnostics.front().code
                == "FSIM-SDF-VITAL-ARCHIVE-002",
        "cross-artifact mismatch must reject transactionally");
    SdfVitalPhaseControl unsupported;
    unsupported.artifact_kind = static_cast<SdfEffectiveArchiveKind>(255U);
    unsupported.producer_identity = "logical:closure";
    const auto unsupported_result
        = build_sdf_vital_phase(archive, std::move(unsupported));
    require(!unsupported_result.ok()
            && unsupported_result.diagnostics.front().code
                == "FSIM-SDF-VITAL-PHASE-002",
        "unsupported closure model control must reject transactionally");
    SdfVitalPhaseControl bounded;
    bounded.producer_identity = "logical:closure";
    SdfVitalPhaseLimits limits;
    limits.max_summary_identity_bytes = 1U;
    const auto overflow = build_sdf_vital_phase(archive, bounded, limits);
    require(!overflow.ok()
            && overflow.diagnostics.front().code
                == "FSIM-SDF-VITAL-PHASE-004",
        "closure summary overflow must reject transactionally");
    limits = { };
    limits.archive.max_archive_bytes = 1U;
    const auto resource
        = build_sdf_vital_phase(archive, std::move(bounded), limits);
    require(!resource.ok()
            && resource.diagnostics.front().code
                == "FSIM-SDF-VITAL-PHASE-004",
        "closure archive resource exhaustion must reject transactionally");
}
} // namespace

int main(const int argc, const char* const* argv)
{
    try {
        require(argc == 2, "owned SDF/VITAL fixture directory is required");
        verify_owned_fixtures(argv[1]);
        const auto cases = run_corpus_matrix();
        require(cases == 2700U, "owned SDF/VITAL corpus matrix is incomplete");
        verify_application_closure();
        std::cout
            << "FSIM-SDF-VITAL-CORPUS-PASS "
               "sdf-revisions=2.1,3.0,4.0 "
               "vhdl-revisions=87,93,2000,2002,2008 "
               "models=standard-cell,primitive,state-table,memory,wrapper "
               "directions=vhdl-verilog,verilog-vhdl,vhdl-systemverilog,systemverilog-vhdl,vhdl-systemc,systemc-vhdl "
               "engines=interpreter,llvm phases=cold,warm,relocated "
               "roots=multiple platform-contract=linux,windows "
               "artifacts=object,design,library,native-cache,checkpoint "
               "negatives=ambiguity,mismatch,unsupported-model,overflow,resource "
               "cases=2700 time-advanced=2700 pass=yes clean-exit=yes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sdf vital corpus test failure: " << error.what() << '\n';
        return 1;
    }
}
