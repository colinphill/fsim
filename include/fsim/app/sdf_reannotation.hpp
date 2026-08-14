// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_drive_timing.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfPendingEventPolicy : std::uint8_t {
    PreserveScheduledTiming
};

enum class SdfTimingCheckStatePolicy : std::uint8_t {
    PreserveHistory
};

struct SdfReannotationLayer {
    std::uint64_t file_precedence { };
    std::uint64_t cell_precedence { };
    std::string file_identity;
    std::string root;
    std::string cell_pattern;
    std::shared_ptr<const SdfDriveTimingApplication> timing;
};

struct SdfReannotationRevision {
    std::string target_identity;
    std::string file_identity;
    std::string root;
    std::string cell_pattern;
    std::uint64_t file_precedence { };
    std::uint64_t cell_precedence { };
    bool timing_check { };
    std::string canonical_identity;

    friend bool operator==(const SdfReannotationRevision&,
        const SdfReannotationRevision&) = default;
};

class SdfReannotationApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    [[nodiscard]] const std::shared_ptr<const SdfDriveTimingApplication>&
    baseline() const noexcept;
    [[nodiscard]] const elaboration::ElaboratedDesign& design() const noexcept;
    [[nodiscard]] std::span<const SdfReannotationRevision> revisions() const
        noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] SdfPendingEventPolicy pending_event_policy() const noexcept;
    [[nodiscard]] SdfTimingCheckStatePolicy timing_check_state_policy() const
        noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

    SdfReannotationApplication(
        std::shared_ptr<const SdfDriveTimingApplication> baseline,
        elaboration::ElaboratedDesign design,
        std::vector<SdfReannotationRevision> revisions,
        std::uint64_t generation,
        std::string semantic_identity);

private:
    std::shared_ptr<const SdfDriveTimingApplication> baseline_;
    elaboration::ElaboratedDesign design_;
    std::vector<SdfReannotationRevision> revisions_;
    std::uint64_t generation_ { };
    std::string semantic_identity_;
};

struct SdfReannotationLimits {
    std::size_t max_files { 16'384U };
    std::size_t max_targets { 1'000'000U };
    std::size_t max_pattern_bytes { 1U << 20U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfReannotationResult {
    std::shared_ptr<const SdfReannotationApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

struct SdfReannotationCommitResult {
    bool committed { };
    std::uint64_t generation { };
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfReannotationResult apply_sdf_reannotation(
    std::shared_ptr<const SdfDriveTimingApplication> baseline,
    std::span<const SdfReannotationLayer> layers,
    std::uint64_t generation,
    SdfReannotationLimits limits = { });

[[nodiscard]] SdfReannotationCommitResult commit_sdf_reannotation(
    const SdfReannotationApplication& application,
    runtime::simir::Interpreter& interpreter);

} // namespace fsim::app
