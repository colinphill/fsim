// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_vital_archive.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfVitalPhaseSurface : std::uint8_t {
    Project,
    CommandLine,
    Tcl,
    CApi,
    CppApi,
    NonProject,
};

enum class SdfVitalExecutionPhase : std::uint8_t {
    Cold,
    Warm,
    Relocated,
};

enum class SdfVitalPhaseEngine : std::uint8_t {
    Interpreter,
    Llvm,
};

struct SdfVitalPhaseControl {
    SdfVitalPhaseSurface surface { SdfVitalPhaseSurface::Project };
    SdfVitalExecutionPhase phase { SdfVitalExecutionPhase::Cold };
    SdfVitalPhaseEngine engine { SdfVitalPhaseEngine::Interpreter };
    SdfEffectiveArchiveKind artifact_kind { SdfEffectiveArchiveKind::Library };
    std::string producer_identity;

    friend bool operator==(const SdfVitalPhaseControl&,
        const SdfVitalPhaseControl&) = default;
};

struct SdfVitalPhaseSummary {
    SdfVitalPhaseControl control;
    std::uint64_t generation { };
    std::size_t record_count { };
    SdfDelaySelection selection { SdfDelaySelection::Typical };
    SdfPendingEventPolicy pending_event_policy {
        SdfPendingEventPolicy::PreserveScheduledTiming
    };
    SdfTimingCheckStatePolicy timing_check_state_policy {
        SdfTimingCheckStatePolicy::PreserveHistory
    };
    std::string archive_identity;
    std::string behavior_identity;
    std::string semantic_identity;

    friend bool operator==(const SdfVitalPhaseSummary&,
        const SdfVitalPhaseSummary&) = default;
};

class SdfVitalPhaseApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    SdfVitalPhaseApplication(
        std::shared_ptr<const SdfVitalArchiveApplication> archive,
        SdfVitalPhaseSummary summary,
        std::vector<std::byte> encoded_archive);

    [[nodiscard]] const std::shared_ptr<const SdfVitalArchiveApplication>&
    archive() const noexcept;
    [[nodiscard]] const SdfVitalPhaseSummary& summary() const noexcept;
    [[nodiscard]] std::span<const std::byte> encoded_archive() const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

private:
    std::shared_ptr<const SdfVitalArchiveApplication> archive_;
    SdfVitalPhaseSummary summary_;
    std::vector<std::byte> encoded_archive_;
};

struct SdfVitalPhaseLimits {
    SdfEffectiveArchiveLimits archive;
    std::size_t max_summary_identity_bytes { 1U << 20U };
};

struct SdfVitalPhaseResult {
    std::shared_ptr<const SdfVitalPhaseApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfVitalPhaseResult build_sdf_vital_phase(
    std::shared_ptr<const SdfVitalArchiveApplication> archive,
    SdfVitalPhaseControl control,
    SdfVitalPhaseLimits limits = { });

} // namespace fsim::app
