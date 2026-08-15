// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/trace_api.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class TraceArchiveKind : std::uint8_t {
    Object,
    Design,
    Library,
    NativeCache,
    Checkpoint
};

struct TraceArchiveSnapshot {
    static constexpr std::uint32_t schema_version = 1U;
    static constexpr std::uint32_t profile_version = 1U;

    std::uint32_t trace_schema { TraceControlApplication::schema_version };
    std::uint32_t profile_schema { profile_version };
    TraceControlSurface surface { TraceControlSurface::CppApi };
    TraceControlPhase phase { TraceControlPhase::Simulate };
    TraceLifecycle lifecycle { TraceLifecycle::Disabled };
    project::TraceFormat requested_format { project::TraceFormat::automatic };
    project::TraceFormat effective_format { project::TraceFormat::automatic };
    project::TraceCompression requested_compression {
        project::TraceCompression::automatic
    };
    project::TraceCompression effective_compression {
        project::TraceCompression::none
    };
    std::filesystem::path output_intent;
    std::uint64_t generation { };
    std::vector<std::string> selection;
    std::vector<TraceControlReportEntry> report;
    std::string semantic_identity;
    std::string declaration_identity;
    std::string profile_identity;

    friend bool operator==(const TraceArchiveSnapshot&,
        const TraceArchiveSnapshot&) = default;
};

struct TraceArchiveLimits {
    std::size_t max_selection_count { 16'384U };
    std::size_t max_report_entries { 1'000'000U };
    std::size_t max_string_bytes { 1U << 20U };
    std::size_t max_archive_bytes { 64U << 20U };
};

struct TraceArchiveEncodeResult {
    std::vector<std::byte> archive;
    std::string archive_identity;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

struct TraceArchiveDecodeResult {
    TraceArchiveSnapshot snapshot;
    std::string archive_identity;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] TraceArchiveSnapshot make_trace_archive_snapshot(
    const TraceControlApplication& application,
    const std::filesystem::path& producer_root);

[[nodiscard]] bool trace_archive_profiles_compatible(
    const TraceArchiveSnapshot& left,
    const TraceArchiveSnapshot& right) noexcept;

[[nodiscard]] TraceControlResult restore_trace_archive_control(
    const TraceArchiveSnapshot& snapshot,
    const std::filesystem::path& consumer_root,
    TraceControlLimits limits = { });

[[nodiscard]] TraceArchiveEncodeResult encode_trace_archive(
    const TraceArchiveSnapshot& snapshot,
    TraceArchiveKind kind,
    TraceArchiveLimits limits = { });

[[nodiscard]] TraceArchiveDecodeResult decode_trace_archive(
    std::span<const std::byte> archive,
    TraceArchiveKind expected_kind,
    project::TraceFormat expected_format = project::TraceFormat::automatic,
    std::string_view expected_profile = { },
    TraceArchiveLimits limits = { });

[[nodiscard]] std::string trace_archive_hex(
    std::span<const std::byte> archive);
[[nodiscard]] std::vector<std::byte> trace_archive_from_hex(
    std::string_view encoded);
[[nodiscard]] std::string_view trace_archive_kind_name(
    TraceArchiveKind kind) noexcept;

} // namespace fsim::app
