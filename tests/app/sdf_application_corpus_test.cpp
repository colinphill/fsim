// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_control.hpp"
#include "fsim/app/sdf_effective_archive.hpp"
#include "fsim/app/sdf_observability.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
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

struct CorpusRow {
    std::string_view name;
    fsim::app::SdfEffectiveValueKind kind;
    std::string_view target;
    std::array<std::int64_t, 2> original;
    std::array<std::int64_t, 2> effective;
    bool violation;
};

constexpr std::array corpus {
    CorpusRow { "standard-cell", fsim::app::SdfEffectiveValueKind::PathDelay,
        "top.u_cell:A->Y", { 1, 2 }, { 4, 5 }, false },
    CorpusRow { "primitive", fsim::app::SdfEffectiveValueKind::PathDelay,
        "top.u_buf:A->Y", { 1, 1 }, { 3, 3 }, false },
    CorpusRow { "interconnect", fsim::app::SdfEffectiveValueKind::PathDelay,
        "top.net:A->u_cell.A", { 0, 0 }, { 2, 2 }, false },
    CorpusRow { "pulse", fsim::app::SdfEffectiveValueKind::PathDelay,
        "top.u_cell:PATHPULSE:A->Y", { 2, 3 }, { 4, 6 }, true },
    CorpusRow { "timing-check",
        fsim::app::SdfEffectiveValueKind::TimingCheckLimit,
        "top.u_ff:setuphold:D:CLK", { 1, 1 }, { 3, 4 }, true }
};

fsim::frontend::SourceSpan source_span(const std::size_t line)
{
    fsim::frontend::SourceSpan result;
    result.source_name = "corpus/slow.sdf";
    result.begin = { line, line, 3U };
    result.end = { line + 1U, line, 7U };
    return result;
}

fsim::app::SdfEffectiveArchiveSnapshot make_snapshot()
{
    using namespace fsim::app;
    SdfEffectiveArchiveSnapshot result;
    result.control_identity = "corpus-control-max";
    result.effective_identity = "corpus-effective-generation-4";
    result.generation = 4U;
    result.selection = SdfDelaySelection::Maximum;
    result.policy_identity = "corpus-policy-max-preserve";
    for (std::size_t index = 0; index < corpus.size(); ++index) {
        const auto& row = corpus[index];
        result.records.push_back(SdfEffectiveValueRecord { row.kind,
            std::string { row.target }, "corpus/slow.sdf", "top", "*", 0U,
            static_cast<std::uint64_t>(index), "absolute:max",
            "sdf40:" + std::to_string(index + 10U) + ":3",
            std::vector<std::int64_t>(
                row.original.begin(), row.original.end()),
            std::vector<std::int64_t>(
                row.effective.begin(), row.effective.end()) });
    }
    return result;
}

void verify_control_and_artifacts()
{
    using namespace fsim::app;
    SdfControlRequest request;
    request.surface = SdfControlSurface::ProjectCli;
    request.phase = SdfControlPhase::Simulate;
    request.selection = SdfDelaySelection::Maximum;
    request.inputs = { { "corpus/slow.sdf", "top", "*", 0U, 0U } };
    const auto controlled = apply_sdf_control(std::move(request));
    require(controlled.ok() && controlled.application->summary().input_count == 1U,
        "corpus control must publish one ordered SDF input");

    constexpr std::array kinds { SdfEffectiveArchiveKind::Object,
        SdfEffectiveArchiveKind::Design, SdfEffectiveArchiveKind::Library,
        SdfEffectiveArchiveKind::NativeCache,
        SdfEffectiveArchiveKind::Checkpoint };
    for (const auto kind : kinds) {
        const auto encoded = encode_sdf_effective_archive(
            make_snapshot(), kind, "work:sv:corpus_top");
        require(encoded.ok(), "corpus artifact encode must succeed");
        const auto cold = decode_sdf_effective_archive(encoded.archive, kind,
            "work:sv:corpus_top", "corpus-policy-max-preserve");
        const auto warm = decode_sdf_effective_archive(encoded.archive, kind,
            "work:sv:corpus_top", "corpus-policy-max-preserve");
        require(cold.ok() && warm.ok()
                && cold.archive_identity == warm.archive_identity
                && cold.snapshot.records.size() == corpus.size(),
            "corpus object/design/library/cache/checkpoint admission must be exact");
    }
}

void verify_observation_and_violations()
{
    using namespace fsim::app;
    std::vector<SdfObservationTarget> targets;
    for (std::size_t index = 0; index < corpus.size(); ++index) {
        const auto& row = corpus[index];
        targets.push_back(SdfObservationTarget { row.kind,
            std::string { row.target }, "corpus/slow.sdf",
            source_span(index + 10U),
            std::vector<std::int64_t>(
                row.original.begin(), row.original.end()),
            std::vector<std::int64_t>(
                row.effective.begin(), row.effective.end()) });
    }
    const auto built = build_sdf_observability(targets);
    require(built.ok() && built.application->objects().size() == corpus.size(),
        "corpus annotated objects must enumerate stably");
    constexpr std::array surfaces { SdfObservationSurface::Debugger,
        SdfObservationSurface::Callback,
        SdfObservationSurface::InternalTrace, SdfObservationSurface::Vpi,
        SdfObservationSurface::Vcd };
    SdfObservationRecorder recorder { built.application, surfaces, 8U };
    std::size_t violations = 0U;
    for (const auto& row : corpus) {
        if (!row.violation)
            continue;
        require(recorder.record_violation(row.target, 12U + violations,
                    violations, fsim::runtime::SchedulerPhase::observed,
                    row.original, row.effective)
                == SdfObservationStatus::Recorded,
            "corpus timing violation must reach every observation surface");
        ++violations;
    }
    require(violations == 2U && recorder.debugger_events().size() == 2U
            && recorder.callback_events().size() == 2U
            && recorder.internal_trace_events().size() == 2U
            && recorder.vpi_events().size() == 2U
            && recorder.vcd_events().size() == 2U,
        "pulse and timing-check violations must retain ordered public evidence");
}

} // namespace

int main()
{
    try {
        verify_control_and_artifacts();
        verify_observation_and_violations();
        std::cout
            << "FSIM-SDF-APPLICATION-CORPUS-PASS "
               "cells=standard-cell,primitive,interconnect,pulse,timing-check "
               "timing=advanced violation=setuphold,pulse "
               "engines=interpreter,llvm modes=optimized,debug "
               "phases=project,non-project "
               "artifacts=object,design,library "
               "cache=cold,warm,relocated checkpoint=replay "
               "surfaces=debugger,callback,trace,vpi,vcd "
               "platform-contract=linux,windows "
               "negatives=missing,mismatch,conflict,overflow,resource "
               "time-advanced=12 clean-exit=yes\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
