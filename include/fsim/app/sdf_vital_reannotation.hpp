// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_vital_timing_checks.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfVitalPendingTransactionPolicy : std::uint8_t {
    PreserveScheduledTiming,
    RejectIfPending,
};

enum class SdfVitalTimingStatePolicy : std::uint8_t {
    PreserveHistory,
    ResetHistory,
};

struct SdfVitalReannotationLayer {
    std::uint64_t file_precedence { };
    std::uint64_t cell_precedence { };
    std::string file_identity;
    std::string root;
    std::string cell_pattern;
    std::shared_ptr<const SdfVitalTimingCheckApplication> timing;
};

struct SdfVitalReannotationRevision {
    std::string target_identity;
    std::string file_identity;
    std::string root;
    std::string cell_pattern;
    std::uint64_t file_precedence { };
    std::uint64_t cell_precedence { };
    bool timing_check { };
    std::vector<std::string> generic_identities;
    std::string canonical_identity;

    friend bool operator==(const SdfVitalReannotationRevision&,
        const SdfVitalReannotationRevision&) = default;
};

class SdfVitalReannotationApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    SdfVitalReannotationApplication(
        std::shared_ptr<const SdfVitalTimingCheckApplication> baseline,
        elaboration::ElaboratedDesign design,
        std::vector<SdfVitalScheduledDelay> delays,
        std::vector<SdfVitalScheduledTimingCheck> checks,
        std::vector<SdfVitalTimingGenericValue> generics,
        std::vector<runtime::simir::Interpreter::VitalTimingReannotation> updates,
        std::vector<SdfVitalReannotationRevision> revisions,
        std::uint64_t generation,
        SdfVitalPendingTransactionPolicy pending_policy,
        SdfVitalTimingStatePolicy timing_state_policy,
        std::string semantic_identity);

    [[nodiscard]] const std::shared_ptr<const SdfVitalTimingCheckApplication>&
    baseline() const noexcept;
    [[nodiscard]] const elaboration::ElaboratedDesign& design() const noexcept;
    [[nodiscard]] std::span<const SdfVitalScheduledDelay> delays() const
        noexcept;
    [[nodiscard]] std::span<const SdfVitalScheduledTimingCheck> checks() const
        noexcept;
    [[nodiscard]] std::span<const SdfVitalTimingGenericValue> generics() const
        noexcept;
    [[nodiscard]] std::span<const
        runtime::simir::Interpreter::VitalTimingReannotation>
    updates() const noexcept;
    [[nodiscard]] std::span<const SdfVitalReannotationRevision> revisions()
        const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] SdfVitalPendingTransactionPolicy pending_policy() const
        noexcept;
    [[nodiscard]] SdfVitalTimingStatePolicy timing_state_policy() const
        noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

private:
    std::shared_ptr<const SdfVitalTimingCheckApplication> baseline_;
    elaboration::ElaboratedDesign design_;
    std::vector<SdfVitalScheduledDelay> delays_;
    std::vector<SdfVitalScheduledTimingCheck> checks_;
    std::vector<SdfVitalTimingGenericValue> generics_;
    std::vector<runtime::simir::Interpreter::VitalTimingReannotation> updates_;
    std::vector<SdfVitalReannotationRevision> revisions_;
    std::uint64_t generation_ { };
    SdfVitalPendingTransactionPolicy pending_policy_ {
        SdfVitalPendingTransactionPolicy::PreserveScheduledTiming
    };
    SdfVitalTimingStatePolicy timing_state_policy_ {
        SdfVitalTimingStatePolicy::PreserveHistory
    };
    std::string semantic_identity_;
};

struct SdfVitalReannotationLimits {
    std::size_t max_files { 4096U };
    std::size_t max_targets { 1'000'000U };
    std::size_t max_pattern_bytes { 1U << 20U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfVitalReannotationResult {
    std::shared_ptr<const SdfVitalReannotationApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

struct SdfVitalReannotationCommitResult {
    bool committed { };
    std::uint64_t generation { };
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfVitalReannotationResult apply_sdf_vital_reannotation(
    std::shared_ptr<const SdfVitalTimingCheckApplication> baseline,
    std::span<const SdfVitalReannotationLayer> layers,
    std::uint64_t generation,
    SdfVitalPendingTransactionPolicy pending_policy
        = SdfVitalPendingTransactionPolicy::PreserveScheduledTiming,
    SdfVitalTimingStatePolicy timing_state_policy
        = SdfVitalTimingStatePolicy::PreserveHistory,
    SdfVitalReannotationLimits limits = { });

[[nodiscard]] SdfVitalReannotationCommitResult commit_sdf_vital_reannotation(
    const SdfVitalReannotationApplication& application,
    runtime::simir::Interpreter& interpreter);

} // namespace fsim::app
