// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_vital_phases.hpp"

#include <string>
#include <utility>

namespace fsim::app {
namespace {
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message)
    {
        diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), { }, { } });
    }

    void append_field(std::string& target, const std::string_view value)
    {
        target += std::to_string(value.size());
        target.push_back(':');
        target.append(value);
    }

    [[nodiscard]] std::string_view surface_name(
        const SdfVitalPhaseSurface surface) noexcept
    {
        switch (surface) {
        case SdfVitalPhaseSurface::Project:
            return "project";
        case SdfVitalPhaseSurface::CommandLine:
            return "cli";
        case SdfVitalPhaseSurface::Tcl:
            return "tcl";
        case SdfVitalPhaseSurface::CApi:
            return "c-api";
        case SdfVitalPhaseSurface::CppApi:
            return "cpp-api";
        case SdfVitalPhaseSurface::NonProject:
            return "non-project";
        }
        return { };
    }

    [[nodiscard]] std::string_view phase_name(
        const SdfVitalExecutionPhase phase) noexcept
    {
        switch (phase) {
        case SdfVitalExecutionPhase::Cold:
            return "cold";
        case SdfVitalExecutionPhase::Warm:
            return "warm";
        case SdfVitalExecutionPhase::Relocated:
            return "relocated";
        }
        return { };
    }

    [[nodiscard]] std::string_view engine_name(
        const SdfVitalPhaseEngine engine) noexcept
    {
        switch (engine) {
        case SdfVitalPhaseEngine::Interpreter:
            return "interpreter";
        case SdfVitalPhaseEngine::Llvm:
            return "llvm";
        }
        return { };
    }

    [[nodiscard]] bool valid_kind(const SdfEffectiveArchiveKind kind) noexcept
    {
        switch (kind) {
        case SdfEffectiveArchiveKind::Object:
        case SdfEffectiveArchiveKind::Design:
        case SdfEffectiveArchiveKind::Library:
        case SdfEffectiveArchiveKind::NativeCache:
        case SdfEffectiveArchiveKind::Checkpoint:
            return true;
        }
        return false;
    }

    [[nodiscard]] std::string_view kind_name(
        const SdfEffectiveArchiveKind kind) noexcept
    {
        switch (kind) {
        case SdfEffectiveArchiveKind::Object:
            return "object";
        case SdfEffectiveArchiveKind::Design:
            return "design";
        case SdfEffectiveArchiveKind::Library:
            return "library";
        case SdfEffectiveArchiveKind::NativeCache:
            return "native-cache";
        case SdfEffectiveArchiveKind::Checkpoint:
            return "checkpoint";
        }
        return { };
    }

    void translate_archive_diagnostics(std::vector<Diagnostic>& diagnostics)
    {
        for (auto& diagnostic : diagnostics) {
            diagnostic.code = diagnostic.code == "FSIM-SDF-VITAL-ARCHIVE-004"
                ? "FSIM-SDF-VITAL-PHASE-004"
                : "FSIM-SDF-VITAL-PHASE-003";
        }
    }
} // namespace

SdfVitalPhaseApplication::SdfVitalPhaseApplication(
    std::shared_ptr<const SdfVitalArchiveApplication> archive,
    SdfVitalPhaseSummary summary, std::vector<std::byte> encoded_archive)
    : archive_(std::move(archive))
    , summary_(std::move(summary))
    , encoded_archive_(std::move(encoded_archive))
{
}

const std::shared_ptr<const SdfVitalArchiveApplication>&
SdfVitalPhaseApplication::archive() const noexcept
{
    return archive_;
}

const SdfVitalPhaseSummary& SdfVitalPhaseApplication::summary() const noexcept
{
    return summary_;
}

std::span<const std::byte> SdfVitalPhaseApplication::encoded_archive() const
    noexcept
{
    return encoded_archive_;
}

std::string_view SdfVitalPhaseApplication::semantic_identity() const noexcept
{
    return summary_.semantic_identity;
}

bool SdfVitalPhaseResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfVitalPhaseResult build_sdf_vital_phase(
    std::shared_ptr<const SdfVitalArchiveApplication> archive,
    SdfVitalPhaseControl control, const SdfVitalPhaseLimits limits)
{
    SdfVitalPhaseResult result;
    if (!archive || archive->semantic_identity().empty()
        || archive->snapshot().records.empty()
        || control.producer_identity.empty()
        || limits.archive.max_records == 0U
        || limits.archive.max_values == 0U
        || limits.archive.max_archive_bytes == 0U
        || limits.archive.max_identity_bytes == 0U
        || limits.max_summary_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-PHASE-001",
            "SDF VITAL phase execution requires a complete archive, control and nonzero limits");
        return result;
    }
    const auto surface = surface_name(control.surface);
    const auto phase = phase_name(control.phase);
    const auto engine = engine_name(control.engine);
    if (surface.empty() || phase.empty() || engine.empty()
        || !valid_kind(control.artifact_kind)) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-PHASE-002",
            "SDF VITAL phase execution received an unsupported surface, phase, engine or artifact kind");
        return result;
    }
    auto encoded = encode_sdf_vital_archive(*archive, control.artifact_kind,
        control.producer_identity, limits.archive);
    if (!encoded.ok()) {
        translate_archive_diagnostics(encoded.diagnostics);
        result.diagnostics = std::move(encoded.diagnostics);
        return result;
    }
    const auto& snapshot = archive->snapshot();
    SdfVitalPhaseSummary summary;
    summary.control = control;
    summary.generation = snapshot.generation;
    summary.record_count = snapshot.records.size();
    summary.selection = snapshot.selection;
    summary.pending_event_policy = snapshot.pending_event_policy;
    summary.timing_check_state_policy = snapshot.timing_check_state_policy;
    summary.archive_identity = encoded.archive_identity;
    summary.behavior_identity = "sdf-vital-phase-behavior-v1";
    append_field(summary.behavior_identity, archive->semantic_identity());
    append_field(summary.behavior_identity, summary.archive_identity);
    append_field(summary.behavior_identity, snapshot.control_identity);
    append_field(summary.behavior_identity, snapshot.effective_identity);
    append_field(summary.behavior_identity, snapshot.policy_identity);
    append_field(summary.behavior_identity, std::to_string(snapshot.generation));
    append_field(summary.behavior_identity,
        std::to_string(static_cast<unsigned>(snapshot.selection)));
    append_field(summary.behavior_identity,
        std::to_string(snapshot.records.size()));
    summary.semantic_identity = "sdf-vital-phase-summary-v1";
    append_field(summary.semantic_identity, summary.behavior_identity);
    append_field(summary.semantic_identity, surface);
    append_field(summary.semantic_identity, phase);
    append_field(summary.semantic_identity, engine);
    append_field(summary.semantic_identity, kind_name(control.artifact_kind));
    if (summary.behavior_identity.size() > limits.max_summary_identity_bytes
        || summary.semantic_identity.size()
            > limits.max_summary_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-PHASE-004",
            "SDF VITAL phase summary exceeds its configured identity limit");
        return result;
    }
    result.application = std::make_shared<const SdfVitalPhaseApplication>(
        std::move(archive), std::move(summary), std::move(encoded.archive));
    return result;
}

} // namespace fsim::app
