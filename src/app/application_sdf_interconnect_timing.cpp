// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_interconnect_timing.hpp"

#include <algorithm>
#include <array>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::app {
namespace {
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::SourceSpan;

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message, const SourceSpan& span)
    {
        diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), span, { } });
    }

    void append_field(std::string& target, const std::string_view value)
    {
        target += std::to_string(value.size());
        target.push_back(':');
        target.append(value);
    }

    [[nodiscard]] bool interconnect_target(
        const SdfTimingTargetKind kind) noexcept
    {
        return kind == SdfTimingTargetKind::Interconnect
            || kind == SdfTimingTargetKind::Port
            || kind == SdfTimingTargetKind::Mipd
            || kind == SdfTimingTargetKind::Device;
    }

    [[nodiscard]] bool legal_delay_count(const std::size_t count) noexcept
    {
        static constexpr auto counts
            = std::to_array<std::size_t>({ 1U, 2U, 3U, 6U, 12U });
        return std::ranges::find(counts, count) != counts.end();
    }

    [[nodiscard]] const elaboration::SignalInfo* find_signal(
        const elaboration::ElaboratedDesign& elaborated,
        const runtime::simir::SignalId signal)
    {
        const auto& signals = elaborated.signals();
        const auto found
            = std::ranges::find(signals, signal, &elaboration::SignalInfo::id);
        return found == signals.end() ? nullptr : &*found;
    }

    [[nodiscard]] bool process_drives(
        const runtime::simir::Process& process,
        const runtime::simir::SignalId signal)
    {
        if (std::ranges::any_of(process.driver_regions,
                [signal](const auto& region) { return region.signal == signal; })) {
            return true;
        }
        return process.switch_target == signal
            || (process.switch_bidirectional && process.switch_source == signal);
    }

    [[nodiscard]] std::vector<runtime::simir::ProcessId> driver_processes(
        const elaboration::ElaboratedDesign& elaborated,
        const std::vector<SdfResolvedEndpoint>& endpoints)
    {
        std::vector<runtime::simir::ProcessId> result;
        for (const auto& process : elaborated.processes()) {
            if (std::ranges::any_of(endpoints, [&](const auto& endpoint) {
                    return process_drives(process, endpoint.signal);
                })) {
                result.push_back(process.id);
            }
        }
        std::ranges::sort(result);
        result.erase(std::ranges::unique(result).begin(), result.end());
        return result;
    }

    [[nodiscard]] bool role_shape(const SdfPlannedAnnotation& annotation)
    {
        const auto count = [&](const SdfEndpointRole role) {
            return static_cast<std::size_t>(std::ranges::count(
                annotation.endpoints, role, &SdfResolvedEndpoint::role));
        };
        if (annotation.construct_kind == frontend::SdfConstructKind::Interconnect) {
            return annotation.endpoints.size() == 2U
                && count(SdfEndpointRole::InterconnectSource) == 1U
                && count(SdfEndpointRole::InterconnectDestination) == 1U;
        }
        if (annotation.construct_kind == frontend::SdfConstructKind::NetDelay)
            return !annotation.endpoints.empty()
                && count(SdfEndpointRole::Net) == annotation.endpoints.size();
        if (annotation.target_kind == SdfTimingTargetKind::Port)
            return annotation.endpoints.size() == 1U
                && (count(SdfEndpointRole::Input) == 1U
                    || count(SdfEndpointRole::Output) == 1U);
        if (annotation.target_kind == SdfTimingTargetKind::Mipd)
            return !annotation.endpoints.empty()
                && count(SdfEndpointRole::Net) == annotation.endpoints.size();
        return annotation.target_kind == SdfTimingTargetKind::Device
            && !annotation.endpoints.empty()
            && count(SdfEndpointRole::Device) == annotation.endpoints.size();
    }

    [[nodiscard]] bool validate_endpoint(
        const SdfResolvedEndpoint& endpoint,
        const elaboration::ElaboratedDesign& elaborated,
        std::vector<Diagnostic>& diagnostics, const SourceSpan& span)
    {
        const auto* signal = find_signal(elaborated, endpoint.signal);
        const bool verilog = endpoint.language == SdfScopeRootLanguage::Verilog
            || endpoint.language == SdfScopeRootLanguage::SystemVerilog;
        const bool hdl_object
            = endpoint.object_kind == SdfEndpointObjectKind::HdlPort
            || endpoint.object_kind == SdfEndpointObjectKind::HdlNet;
        const bool kind_matches = signal
            && ((endpoint.object_kind == SdfEndpointObjectKind::HdlPort
                    && signal->is_port)
                || (endpoint.object_kind == SdfEndpointObjectKind::HdlNet
                    && !signal->is_port));
        const bool direction_matches = signal
            && (endpoint.object_kind != SdfEndpointObjectKind::HdlPort
                || (endpoint.direction != frontend::PortDirection::Unknown
                    && endpoint.direction == signal->direction));
        const bool select_matches = !endpoint.select
            || (endpoint.select->width != 0U
                && endpoint.select->width <= endpoint.object_width);
        if (!signal || !verilog || !hdl_object || !kind_matches
            || !direction_matches || endpoint.instance_path.empty()
            || endpoint.object_path.empty() || endpoint.object_width == 0U
            || endpoint.object_width != (signal ? signal->width : 0U)
            || !select_matches) {
            diagnose(diagnostics, "FSIM-SDF-INTERCONNECT-002",
                "SDF net/port/device endpoint is stale, ambiguous, unsupported, or directionally incompatible",
                span);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validate_endpoints(
        const SdfPlannedAnnotation& annotation,
        const elaboration::ElaboratedDesign& elaborated,
        const SdfInterconnectTimingLimits& limits,
        std::vector<Diagnostic>& diagnostics)
    {
        if (annotation.endpoints.empty()
            || annotation.endpoints.size() > limits.max_endpoints_per_target
            || !role_shape(annotation)) {
            diagnose(diagnostics,
                annotation.endpoints.size() > limits.max_endpoints_per_target
                    ? "FSIM-SDF-INTERCONNECT-004"
                    : "FSIM-SDF-INTERCONNECT-002",
                "SDF net/port/device endpoint shape is empty, incompatible, or over its configured limit",
                annotation.source);
            return false;
        }
        std::set<std::string> owners;
        bool valid = true;
        for (const auto& endpoint : annotation.endpoints) {
            std::string owner = endpoint.instance_path;
            append_field(owner, endpoint.object_path);
            append_field(owner, std::to_string(endpoint.signal));
            append_field(owner, std::to_string(static_cast<unsigned>(endpoint.role)));
            if (!owners.insert(std::move(owner)).second) {
                diagnose(diagnostics, "FSIM-SDF-INTERCONNECT-002",
                    "SDF net/port/device annotation contains an ambiguous duplicate endpoint",
                    annotation.source);
                valid = false;
            }
            valid = validate_endpoint(
                        endpoint, elaborated, diagnostics, annotation.source)
                && valid;
        }
        return valid;
    }

    [[nodiscard]] std::string applied_identity(
        const SdfPlannedAnnotation& annotation,
        const std::vector<runtime::simir::ProcessId>& drivers)
    {
        std::string result = "sdf-interconnect-timing-v1";
        append_field(result, annotation.canonical_identity);
        append_field(result, annotation.target_identity);
        append_field(result,
            std::to_string(static_cast<unsigned>(annotation.target_kind)));
        append_field(result,
            std::to_string(static_cast<unsigned>(annotation.delay_mode)));
        for (const auto driver : drivers)
            append_field(result, std::to_string(driver));
        for (const auto delay : annotation.after_ticks)
            append_field(result, std::to_string(delay));
        return result;
    }

    [[nodiscard]] std::string application_identity(
        const SdfAnnotationPlan& plan,
        const std::vector<SdfAppliedInterconnectTiming>& timings)
    {
        std::string result = "sdf-interconnect-application-v1";
        append_field(result, plan.semantic_identity());
        for (const auto& timing : timings)
            append_field(result, timing.canonical_identity);
        return result;
    }
} // namespace

const std::shared_ptr<const SdfAnnotationPlan>&
SdfInterconnectTimingApplication::plan() const noexcept
{
    return plan_;
}

std::span<const SdfAppliedInterconnectTiming>
SdfInterconnectTimingApplication::timings() const noexcept
{
    return timings_;
}

const SdfAppliedInterconnectTiming*
SdfInterconnectTimingApplication::find_target(
    const std::string_view identity) const noexcept
{
    const auto found = std::ranges::find(
        timings_, identity, &SdfAppliedInterconnectTiming::target_identity);
    return found == timings_.end() ? nullptr : &*found;
}

std::string_view SdfInterconnectTimingApplication::semantic_identity() const
    noexcept
{
    return semantic_identity_;
}

SdfInterconnectTimingApplication::SdfInterconnectTimingApplication(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    std::vector<SdfAppliedInterconnectTiming> timings,
    std::string semantic_identity)
    : plan_(std::move(plan))
    , timings_(std::move(timings))
    , semantic_identity_(std::move(semantic_identity))
{
}

bool SdfInterconnectTimingResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfInterconnectTimingResult apply_sdf_interconnect_timing(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    const elaboration::ElaboratedDesign& elaborated,
    const SdfInterconnectTimingLimits limits)
{
    SdfInterconnectTimingResult result;
    if (!plan || !plan->summary() || !plan->summary()->endpoint_resolution()
        || !plan->summary()->endpoint_resolution()->cells()
        || !plan->summary()->endpoint_resolution()->cells()->scope()
        || !plan->summary()->endpoint_resolution()->cells()->scope()->normalized_ir()
        || plan->summary()->semantic_identity().empty()
        || plan->semantic_identity().empty()
        || plan->summary()->annotation_count() != plan->annotations().size()
        || limits.max_targets == 0U || limits.max_endpoints_per_target == 0U
        || limits.max_drivers_per_target == 0U
        || limits.max_values_per_target == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-INTERCONNECT-001",
            "SDF interconnect timing application requires a complete plan and nonzero limits",
            { });
        return result;
    }

    std::vector<SdfAppliedInterconnectTiming> timings;
    std::set<std::string> target_identities;
    std::size_t identity_bytes { };
    for (const auto& annotation : plan->annotations()) {
        if (!interconnect_target(annotation.target_kind))
            continue;
        if (timings.size() >= limits.max_targets) {
            diagnose(result.diagnostics, "FSIM-SDF-INTERCONNECT-004",
                "SDF interconnect timing application exceeds its configured target limit",
                annotation.source);
            continue;
        }
        bool valid
            = validate_endpoints(annotation, elaborated, limits, result.diagnostics);
        if (annotation.delay_mode == SdfDelayApplicationMode::None
            || !legal_delay_count(annotation.after_ticks.size())
            || annotation.after_ticks.size() > limits.max_values_per_target) {
            diagnose(result.diagnostics, "FSIM-SDF-INTERCONNECT-003",
                "SDF net/port/device transition profile has an invalid mode or delay-list arity",
                annotation.source);
            valid = false;
        }
        if (!target_identities.insert(annotation.target_identity).second) {
            diagnose(result.diagnostics, "FSIM-SDF-INTERCONNECT-001",
                "SDF interconnect timing application repeats one timing target identity",
                annotation.source);
            valid = false;
        }
        auto drivers = driver_processes(elaborated, annotation.endpoints);
        if (drivers.size() > limits.max_drivers_per_target) {
            diagnose(result.diagnostics, "FSIM-SDF-INTERCONNECT-004",
                "SDF interconnect timing application exceeds its configured driver-owner limit",
                annotation.source);
            valid = false;
        }
        if (!valid)
            continue;

        SdfAppliedInterconnectTiming applied;
        applied.target_kind = annotation.target_kind;
        applied.mode = annotation.delay_mode;
        applied.target_instance_path = annotation.target_instance_path;
        applied.target_identity = annotation.target_identity;
        applied.endpoints = annotation.endpoints;
        applied.driver_processes = std::move(drivers);
        applied.transition_delays.assign(
            annotation.after_ticks.begin(), annotation.after_ticks.end());
        applied.annotation_source = annotation.source;
        applied.annotation_identity = annotation.canonical_identity;
        applied.canonical_identity
            = applied_identity(annotation, applied.driver_processes);
        if (applied.canonical_identity.size() > limits.max_identity_bytes
            || identity_bytes
                > limits.max_identity_bytes - applied.canonical_identity.size()) {
            diagnose(result.diagnostics, "FSIM-SDF-INTERCONNECT-004",
                "SDF interconnect timing application exceeds its configured identity-byte limit",
                annotation.source);
            continue;
        }
        identity_bytes += applied.canonical_identity.size();
        timings.push_back(std::move(applied));
    }
    if (!result.diagnostics.empty())
        return result;
    const auto identity = application_identity(*plan, timings);
    if (identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-INTERCONNECT-004",
            "SDF interconnect timing application semantic identity exceeds its configured limit",
            { });
        return result;
    }
    result.application = std::make_shared<const SdfInterconnectTimingApplication>(
        std::move(plan), std::move(timings), identity);
    return result;
}

} // namespace fsim::app
