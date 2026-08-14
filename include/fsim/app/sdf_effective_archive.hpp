// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_control.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfEffectiveArchiveKind : std::uint8_t {
    Object,
    Design,
    Library,
    NativeCache,
    Checkpoint
};

enum class SdfEffectiveValueKind : std::uint8_t {
    PathDelay,
    TimingCheckLimit
};

struct SdfEffectiveValueRecord {
    SdfEffectiveValueKind kind { SdfEffectiveValueKind::PathDelay };
    std::string target_identity;
    std::string source_identity;
    std::string root;
    std::string cell_pattern;
    std::uint64_t file_precedence { };
    std::uint64_t cell_precedence { };
    std::string policy_identity;
    std::string provenance_identity;
    std::vector<std::int64_t> original_values;
    std::vector<std::int64_t> effective_values;

    friend bool operator==(const SdfEffectiveValueRecord&,
        const SdfEffectiveValueRecord&) = default;
};

struct SdfEffectiveArchiveSnapshot {
    static constexpr std::uint32_t schema_version = 1U;

    std::string control_identity;
    std::string effective_identity;
    std::uint64_t generation { };
    SdfDelaySelection selection { SdfDelaySelection::Typical };
    SdfPendingEventPolicy pending_event_policy {
        SdfPendingEventPolicy::PreserveScheduledTiming
    };
    SdfTimingCheckStatePolicy timing_check_state_policy {
        SdfTimingCheckStatePolicy::PreserveHistory
    };
    std::string policy_identity;
    std::vector<SdfEffectiveValueRecord> records;

    friend bool operator==(const SdfEffectiveArchiveSnapshot&,
        const SdfEffectiveArchiveSnapshot&) = default;
};

struct SdfEffectiveArchiveLimits {
    std::size_t max_records { 1'000'000U };
    std::size_t max_values { 12'000'000U };
    std::size_t max_string_bytes { 1U << 20U };
    std::size_t max_archive_bytes { 64U << 20U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfEffectiveArchiveEncodeResult {
    std::vector<std::byte> archive;
    std::string archive_identity;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

struct SdfEffectiveArchiveDecodeResult {
    SdfEffectiveArchiveSnapshot snapshot;
    std::string archive_identity;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfEffectiveArchiveEncodeResult encode_sdf_effective_archive(
    SdfEffectiveArchiveSnapshot snapshot,
    SdfEffectiveArchiveKind kind,
    std::string_view producer_identity,
    SdfEffectiveArchiveLimits limits = { });

[[nodiscard]] SdfEffectiveArchiveDecodeResult decode_sdf_effective_archive(
    std::span<const std::byte> archive,
    SdfEffectiveArchiveKind expected_kind,
    std::string_view expected_producer_identity,
    std::string_view expected_policy_identity,
    SdfEffectiveArchiveLimits limits = { });

[[nodiscard]] std::string_view sdf_effective_archive_kind_name(
    SdfEffectiveArchiveKind kind) noexcept;

} // namespace fsim::app
