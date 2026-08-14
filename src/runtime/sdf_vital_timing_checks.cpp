// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_vital_timing_checks.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fsim::app {
namespace {
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::SourceSpan;
    using runtime::simir::Process;
    using runtime::simir::VitalTimingCheck;
    using runtime::simir::VitalTimingCheckKind;

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message, const SourceSpan& span = { })
    {
        diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), span, { } });
    }

    void append_field(std::string& identity, const std::string_view field)
    {
        identity += '|';
        identity += std::to_string(field.size());
        identity += ':';
        identity += field;
    }

    [[nodiscard]] bool valid_kind(const VitalTimingCheckKind kind)
    {
        switch (kind) {
        case VitalTimingCheckKind::setup_hold:
        case VitalTimingCheckKind::recovery_removal:
        case VitalTimingCheckKind::period_pulse:
        case VitalTimingCheckKind::in_phase_skew:
        case VitalTimingCheckKind::out_phase_skew:
            return true;
        }
        return false;
    }

    using ValueGroups = std::map<std::string_view,
        std::vector<const SdfVitalEffectiveTimingValue*>, std::less<>>;

    [[nodiscard]] ValueGroups group_check_values(
        const SdfVitalPrecedenceApplication& precedence)
    {
        ValueGroups groups;
        for (const auto& value : precedence.values()) {
            if (value.timing_check)
                groups[value.call_identity].push_back(&value);
        }
        for (auto& [identity, values] : groups) {
            (void)identity;
            std::ranges::sort(values, { },
                &SdfVitalEffectiveTimingValue::value_index);
        }
        return groups;
    }

    [[nodiscard]] bool apply_check(Process& process,
        const SdfVitalPathTimingRecord& source,
        const std::vector<const SdfVitalEffectiveTimingValue*>& values,
        SdfVitalScheduledTimingCheck& scheduled,
        std::vector<Diagnostic>& diagnostics)
    {
        if (source.call.instruction >= process.operations.size()) {
            diagnose(diagnostics, "FSIM-SDF-VITAL-CHECK-002",
                "SDF VITAL timing-check instruction is outside its process",
                source.source);
            return false;
        }
        auto* operation = runtime::simir::operation_get_if<VitalTimingCheck>(
            &process.operations[source.call.instruction]);
        if (operation == nullptr
            || source.before_check_ticks.size() != operation->limits.size()
            || values.size() != operation->limits.size()) {
            diagnose(diagnostics, "FSIM-SDF-VITAL-CHECK-002",
                "SDF VITAL timing-check kind or value arity is stale",
                source.source);
            return false;
        }
        if (!valid_kind(operation->kind)) {
            diagnose(diagnostics, "FSIM-SDF-VITAL-CHECK-004",
                "SDF VITAL timing-check kind is unsupported", source.source);
            return false;
        }
        scheduled.call = source.call;
        scheduled.kind = operation->kind;
        scheduled.endpoint_signals = source.endpoint_signals;
        scheduled.edge_identities = source.edge_identities;
        scheduled.condition_identity = source.condition_identity;
        for (std::size_t index = 0; index < values.size(); ++index) {
            const auto* value = values[index];
            if (value->value_index != index || !value->timing_check
                || value->call_identity != source.call.canonical_identity
                || value->source_check_ticks != source.before_check_ticks[index]
                || source.before_check_ticks[index] < 0
                || !value->effective_check_ticks
                || *value->effective_check_ticks < 0
                || (value->no_annotation
                    && value->effective_check_ticks
                        != value->source_check_ticks)) {
                diagnose(diagnostics, "FSIM-SDF-VITAL-CHECK-003",
                    "SDF VITAL timing-check value is negative, missing, stale, or noncontiguous",
                    source.source);
                return false;
            }
            const auto source_limit = static_cast<std::uint64_t>(
                source.before_check_ticks[index]);
            const auto effective_limit = static_cast<std::uint64_t>(
                *value->effective_check_ticks);
            if (operation->limits[index] != source_limit) {
                diagnose(diagnostics, "FSIM-SDF-VITAL-CHECK-002",
                    "SDF VITAL timing-check source limit no longer matches its call",
                    source.source);
                return false;
            }
            scheduled.source_limits[index] = source_limit;
            scheduled.effective_limits[index] = effective_limit;
            operation->limits[index] = effective_limit;
        }
        scheduled.check_enabled = operation->check_enabled;
        scheduled.enables = operation->enables;
        scheduled.x_on = operation->x_on;
        scheduled.message_on = operation->message_on;
        scheduled.severity = operation->severity;
        scheduled.message = operation->message;
        scheduled.violation_source = operation->source;
        scheduled.annotation_source = source.source;
        auto identity = std::string { "sdf-vital-scheduled-check-v1" };
        append_field(identity, source.call.canonical_identity);
        append_field(identity,
            std::to_string(static_cast<unsigned>(scheduled.kind)));
        append_field(identity, scheduled.check_enabled ? "enabled" : "disabled");
        append_field(identity, scheduled.message);
        for (const auto* value : values)
            append_field(identity, value->canonical_identity);
        scheduled.canonical_identity = std::move(identity);
        return true;
    }
} // namespace

SdfVitalTimingCheckApplication::SdfVitalTimingCheckApplication(
    std::shared_ptr<const SdfVitalSchedulingApplication> scheduling,
    elaboration::ElaboratedDesign design,
    std::vector<SdfVitalScheduledTimingCheck> checks,
    std::string semantic_identity)
    : scheduling_(std::move(scheduling))
    , design_(std::move(design))
    , checks_(std::move(checks))
    , semantic_identity_(std::move(semantic_identity))
{
}

const std::shared_ptr<const SdfVitalSchedulingApplication>&
SdfVitalTimingCheckApplication::scheduling() const noexcept
{
    return scheduling_;
}

const elaboration::ElaboratedDesign& SdfVitalTimingCheckApplication::design()
    const noexcept
{
    return design_;
}

std::span<const SdfVitalScheduledTimingCheck>
SdfVitalTimingCheckApplication::checks() const noexcept
{
    return checks_;
}

std::string_view SdfVitalTimingCheckApplication::semantic_identity() const
    noexcept
{
    return semantic_identity_;
}

bool SdfVitalTimingCheckResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfVitalTimingCheckResult apply_sdf_vital_timing_checks(
    std::shared_ptr<const SdfVitalSchedulingApplication> scheduling,
    const SdfVitalTimingCheckLimits limits)
{
    SdfVitalTimingCheckResult result;
    if (!scheduling || !scheduling->precedence()
        || !scheduling->precedence()->source()
        || !scheduling->precedence()->source()->paths()
        || scheduling->semantic_identity().empty()
        || limits.max_checks == 0U || limits.max_values == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-CHECK-001",
            "SDF VITAL timing checks require complete scheduling and nonzero limits");
        return result;
    }
    const auto& precedence = *scheduling->precedence();
    const auto groups = group_check_values(precedence);
    if (groups.size() > limits.max_checks
        || precedence.values().size() > limits.max_values) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-CHECK-006",
            "SDF VITAL timing checks exceed their call or value limit");
        return result;
    }
    auto state = scheduling->design().state();
    std::unordered_map<runtime::simir::ProcessId, Process*> processes;
    for (auto& process : state.processes) {
        if (!processes.emplace(process.id, &process).second) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-CHECK-002",
                "SDF VITAL timing checks found duplicate process ownership");
            return result;
        }
    }
    std::vector<SdfVitalScheduledTimingCheck> checks;
    checks.reserve(groups.size());
    std::unordered_set<std::string_view> owners;
    std::size_t identity_bytes { };
    for (const auto& source : precedence.source()->paths()->records()) {
        if (source.call.kind != SdfVitalCallKind::TimingCheck)
            continue;
        const auto values = groups.find(source.call.canonical_identity);
        const auto process = processes.find(source.call.process);
        if (source.call.canonical_identity.empty()
            || !owners.insert(source.call.canonical_identity).second
            || values == groups.end() || process == processes.end()) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-CHECK-002",
                "SDF VITAL timing check has stale or duplicate ownership",
                source.source);
            return result;
        }
        SdfVitalScheduledTimingCheck check;
        if (!apply_check(*process->second, source, values->second, check,
                result.diagnostics)) {
            return result;
        }
        if (check.canonical_identity.size() > limits.max_identity_bytes
            || identity_bytes > limits.max_identity_bytes
                    - check.canonical_identity.size()) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-CHECK-006",
                "SDF VITAL timing-check identities exceed their byte limit",
                source.source);
            return result;
        }
        identity_bytes += check.canonical_identity.size();
        checks.push_back(std::move(check));
    }
    if (checks.size() != groups.size()) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-CHECK-002",
            "SDF VITAL precedence contains a check without a source owner");
        return result;
    }
    auto design = elaboration::ElaboratedDesign::from_state(std::move(state));
    if (!design) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-CHECK-005",
            "SDF VITAL timing-check publication produced an invalid design");
        return result;
    }
    auto semantic_identity = std::string { "sdf-vital-timing-checks-v1" };
    append_field(semantic_identity, scheduling->semantic_identity());
    for (const auto& check : checks)
        append_field(semantic_identity, check.canonical_identity);
    if (semantic_identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-CHECK-006",
            "SDF VITAL timing-check semantic identity exceeds its limit");
        return result;
    }
    result.application = std::make_shared<const SdfVitalTimingCheckApplication>(
        std::move(scheduling), std::move(*design), std::move(checks),
        std::move(semantic_identity));
    return result;
}

} // namespace fsim::app
