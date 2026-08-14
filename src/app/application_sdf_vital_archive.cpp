// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_vital_archive.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
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

    [[nodiscard]] bool append_ticks(std::vector<std::int64_t>& destination,
        const std::span<const runtime::SimulationTick> values)
    {
        for (const auto value : values) {
            if (value > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max()))
                return false;
            destination.push_back(static_cast<std::int64_t>(value));
        }
        return true;
    }

    [[nodiscard]] SdfDelaySelection selection(
        const SdfVitalReannotationApplication& vital)
    {
        const auto& baseline = vital.baseline();
        if (baseline && baseline->scheduling()
            && baseline->scheduling()->precedence()) {
            return baseline->scheduling()->precedence()->policy().command_selection;
        }
        return SdfDelaySelection::Typical;
    }

    void translate_diagnostics(std::vector<Diagnostic>& diagnostics)
    {
        for (auto& diagnostic : diagnostics) {
            if (diagnostic.code == "FSIM-SDF-EFFECTIVE-001")
                diagnostic.code = "FSIM-SDF-VITAL-ARCHIVE-001";
            else if (diagnostic.code == "FSIM-SDF-EFFECTIVE-002")
                diagnostic.code = "FSIM-SDF-VITAL-ARCHIVE-002";
            else if (diagnostic.code == "FSIM-SDF-EFFECTIVE-003")
                diagnostic.code = "FSIM-SDF-VITAL-ARCHIVE-003";
            else if (diagnostic.code == "FSIM-SDF-EFFECTIVE-004")
                diagnostic.code = "FSIM-SDF-VITAL-ARCHIVE-004";
        }
    }
} // namespace

SdfVitalArchiveApplication::SdfVitalArchiveApplication(
    std::shared_ptr<const SdfVitalReannotationApplication> vital,
    std::shared_ptr<const SdfForeignInterfaceApplication> foreign,
    SdfEffectiveArchiveSnapshot snapshot, std::string semantic_identity)
    : vital_(std::move(vital))
    , foreign_(std::move(foreign))
    , snapshot_(std::move(snapshot))
    , semantic_identity_(std::move(semantic_identity))
{
}

const std::shared_ptr<const SdfVitalReannotationApplication>&
SdfVitalArchiveApplication::vital() const noexcept
{
    return vital_;
}

const std::shared_ptr<const SdfForeignInterfaceApplication>&
SdfVitalArchiveApplication::foreign() const noexcept
{
    return foreign_;
}

const SdfEffectiveArchiveSnapshot& SdfVitalArchiveApplication::snapshot() const
    noexcept
{
    return snapshot_;
}

std::string_view SdfVitalArchiveApplication::semantic_identity() const noexcept
{
    return semantic_identity_;
}

bool SdfVitalArchiveResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfVitalArchiveResult build_sdf_vital_archive(
    std::shared_ptr<const SdfVitalReannotationApplication> vital,
    std::shared_ptr<const SdfForeignInterfaceApplication> foreign,
    const SdfEffectiveArchiveLimits limits)
{
    SdfVitalArchiveResult result;
    if (!vital || !foreign || vital->generation() == 0U
        || vital->semantic_identity().empty() || foreign->objects().empty()
        || foreign->semantic_identity().empty() || limits.max_records == 0U
        || limits.max_values == 0U || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-ARCHIVE-001",
            "VITAL archive requires complete effective applications and nonzero limits");
        return result;
    }
    if (foreign->objects().size() > limits.max_records) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-ARCHIVE-004",
            "VITAL archive exceeds its configured record limit");
        return result;
    }
    SdfEffectiveArchiveSnapshot snapshot;
    snapshot.control_identity = vital->semantic_identity();
    snapshot.effective_identity = foreign->semantic_identity();
    snapshot.generation = vital->generation();
    snapshot.selection = selection(*vital);
    snapshot.pending_event_policy
        = SdfPendingEventPolicy::PreserveScheduledTiming;
    snapshot.timing_check_state_policy
        = SdfTimingCheckStatePolicy::PreserveHistory;
    snapshot.policy_identity = "sdf-vital-archive-policy-v1";
    append_field(snapshot.policy_identity, std::to_string(vital->generation()));
    append_field(snapshot.policy_identity,
        std::to_string(static_cast<unsigned>(snapshot.selection)));
    append_field(snapshot.policy_identity,
        std::to_string(static_cast<unsigned>(snapshot.pending_event_policy)));
    append_field(snapshot.policy_identity, std::to_string(static_cast<unsigned>(snapshot.timing_check_state_policy)));
    append_field(snapshot.policy_identity,
        std::to_string(static_cast<unsigned>(vital->pending_policy())));
    append_field(snapshot.policy_identity,
        std::to_string(static_cast<unsigned>(vital->timing_state_policy())));
    append_field(snapshot.policy_identity, foreign->semantic_identity());
    std::size_t value_count { };
    for (const auto& object : foreign->objects()) {
        if (object.canonical_identity.empty() || object.source_identity.empty()
            || object.effective_ticks.empty()
            || object.effective_ticks.size() > limits.max_values - value_count) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-ARCHIVE-003",
                "VITAL archive contains stale identity or invalid effective values");
            return result;
        }
        SdfEffectiveValueRecord record;
        record.kind = object.timing_kind
                == SdfForeignTimingKind::VitalTimingCheck
            ? SdfEffectiveValueKind::TimingCheckLimit
            : SdfEffectiveValueKind::PathDelay;
        record.target_identity = object.canonical_identity;
        record.source_identity = object.source_identity;
        record.root = object.root_identity;
        record.cell_pattern = object.hierarchy_path;
        record.policy_identity = snapshot.policy_identity;
        record.provenance_identity = vital->semantic_identity();
        if (!append_ticks(record.effective_values, object.effective_ticks)) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-ARCHIVE-003",
                "VITAL archive cannot represent an effective tick as signed portable data");
            return result;
        }
        if (object.timing_kind == SdfForeignTimingKind::VitalDelay) {
            const auto found = std::ranges::find(vital->delays(),
                object.source_identity, &SdfVitalScheduledDelay::canonical_identity);
            if (found != vital->delays().end())
                (void)append_ticks(record.original_values, found->source_delay_ticks);
        } else if (object.timing_kind
            == SdfForeignTimingKind::VitalTimingCheck) {
            const auto found = std::ranges::find(vital->checks(),
                object.source_identity,
                &SdfVitalScheduledTimingCheck::canonical_identity);
            if (found != vital->checks().end())
                record.original_values.assign(
                    found->source_limits.begin(), found->source_limits.end());
        }
        if (record.original_values.empty())
            record.original_values = record.effective_values;
        if (record.original_values.size() != record.effective_values.size()) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-ARCHIVE-003",
                "VITAL archive source/effective value shapes do not match");
            return result;
        }
        value_count += object.effective_ticks.size();
        snapshot.records.push_back(std::move(record));
    }
    std::string identity = "sdf-vital-archive-application-v1";
    append_field(identity, vital->semantic_identity());
    append_field(identity, foreign->semantic_identity());
    append_field(identity, snapshot.policy_identity);
    if (identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-ARCHIVE-004",
            "VITAL archive application identity exceeds its limit");
        return result;
    }
    result.application = std::make_shared<const SdfVitalArchiveApplication>(
        std::move(vital), std::move(foreign), std::move(snapshot), identity);
    return result;
}

SdfEffectiveArchiveEncodeResult encode_sdf_vital_archive(
    const SdfVitalArchiveApplication& application,
    const SdfEffectiveArchiveKind kind,
    const std::string_view producer_identity,
    const SdfEffectiveArchiveLimits limits)
{
    auto result = encode_sdf_effective_archive(
        application.snapshot(), kind, producer_identity, limits);
    translate_diagnostics(result.diagnostics);
    return result;
}

SdfEffectiveArchiveDecodeResult decode_sdf_vital_archive(
    const std::span<const std::byte> archive,
    const SdfEffectiveArchiveKind expected_kind,
    const std::string_view expected_producer_identity,
    const std::string_view expected_policy_identity,
    const SdfEffectiveArchiveLimits limits)
{
    auto result = decode_sdf_effective_archive(archive, expected_kind,
        expected_producer_identity, expected_policy_identity, limits);
    translate_diagnostics(result.diagnostics);
    return result;
}

} // namespace fsim::app
