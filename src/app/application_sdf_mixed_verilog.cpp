// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_mixed_verilog.hpp"

#include <algorithm>
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
    using runtime::simir::Process;
    using runtime::simir::ProcessId;
    using runtime::simir::SignalId;

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

    [[nodiscard]] bool verilog_language(const SdfScopeRootLanguage language)
    {
        return language == SdfScopeRootLanguage::Verilog
            || language == SdfScopeRootLanguage::SystemVerilog;
    }

    [[nodiscard]] const SdfResolvedEndpoint* find_endpoint(
        const SdfAppliedInterconnectTiming& timing, const SignalId signal)
    {
        const auto found = std::ranges::find(
            timing.endpoints, signal, &SdfResolvedEndpoint::signal);
        return found == timing.endpoints.end() ? nullptr : &*found;
    }

    [[nodiscard]] bool process_drives(
        const Process& process, const SignalId signal)
    {
        if (std::ranges::any_of(process.driver_regions,
                [signal](const auto& region) { return region.signal == signal; })) {
            return true;
        }
        return process.switch_target == signal
            || (process.switch_bidirectional && process.switch_source == signal);
    }

    [[nodiscard]] bool process_loads(
        const Process& process, const SignalId signal)
    {
        return std::ranges::any_of(process.static_sensitivity,
            [signal](const auto& sensitivity) {
                return sensitivity.signal == signal;
            });
    }

    template <typename Predicate>
    [[nodiscard]] std::vector<ProcessId> process_owners(
        const elaboration::ElaboratedDesign& elaborated, const SignalId signal,
        Predicate predicate, const std::optional<ProcessId> excluded = std::nullopt)
    {
        std::vector<ProcessId> result;
        for (const auto& process : elaborated.processes()) {
            if ((!excluded || process.id != *excluded)
                && predicate(process, signal)) {
                result.push_back(process.id);
            }
        }
        std::ranges::sort(result);
        result.erase(std::ranges::unique(result).begin(), result.end());
        return result;
    }

    [[nodiscard]] const elaboration::BoundaryConversionInfo* find_conversion(
        const SdfAppliedInterconnectTiming& timing,
        const elaboration::ElaboratedDesign& elaborated,
        std::vector<Diagnostic>& diagnostics)
    {
        const elaboration::BoundaryConversionInfo* selected = nullptr;
        for (const auto& endpoint : timing.endpoints) {
            if (!endpoint.conversion || !endpoint.conversion_peer)
                continue;
            for (const auto& conversion : elaborated.boundary_conversions()) {
                const bool signals_match
                    = (conversion.formal_signal == endpoint.signal
                          && conversion.actual_signal == *endpoint.conversion_peer)
                    || (conversion.actual_signal == endpoint.signal
                        && conversion.formal_signal == *endpoint.conversion_peer);
                if (!signals_match || conversion.kind != *endpoint.conversion)
                    continue;
                if (selected != nullptr && selected != &conversion) {
                    diagnose(diagnostics, "FSIM-SDF-MIXED-VERILOG-002",
                        "mixed VHDL/Verilog timing resolves more than one boundary conversion",
                        timing.annotation_source);
                    return nullptr;
                }
                selected = &conversion;
            }
        }
        return selected;
    }

    [[nodiscard]] bool direction_and_signals(
        const elaboration::BoundaryConversionInfo& conversion,
        SignalId& source, SignalId& destination)
    {
        if (conversion.direction == frontend::PortDirection::Input) {
            source = conversion.actual_signal;
            destination = conversion.formal_signal;
            return true;
        }
        if (conversion.direction == frontend::PortDirection::Output
            || conversion.direction == frontend::PortDirection::Buffer) {
            source = conversion.formal_signal;
            destination = conversion.actual_signal;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool validate_adapter(
        const elaboration::BoundaryConversionInfo& conversion,
        const elaboration::ElaboratedDesign& elaborated,
        const SignalId source, const SignalId destination)
    {
        if (conversion.formal_width == 0U || conversion.actual_width == 0U
            || conversion.formal_domain == frontend::ValueDomain::Unknown
            || conversion.actual_domain == frontend::ValueDomain::Unknown) {
            return false;
        }
        if (!conversion.process)
            return conversion.formal_signal == conversion.actual_signal;
        const auto found = std::ranges::find(
            elaborated.processes(), *conversion.process, &Process::id);
        return found != elaborated.processes().end()
            && process_loads(*found, source) && process_drives(*found, destination);
    }

    [[nodiscard]] bool endpoint_languages(
        const SdfAppliedInterconnectTiming& timing, const SignalId source,
        const SignalId destination, SdfScopeRootLanguage& source_language,
        SdfScopeRootLanguage& destination_language)
    {
        const auto* source_endpoint = find_endpoint(timing, source);
        const auto* destination_endpoint = find_endpoint(timing, destination);
        if (source_endpoint == nullptr || destination_endpoint == nullptr
            || source_endpoint->role != SdfEndpointRole::InterconnectSource
            || destination_endpoint->role
                != SdfEndpointRole::InterconnectDestination) {
            return false;
        }
        source_language = source_endpoint->language;
        destination_language = destination_endpoint->language;
        return (source_language == SdfScopeRootLanguage::Vhdl
                   && verilog_language(destination_language))
            || (verilog_language(source_language)
                && destination_language == SdfScopeRootLanguage::Vhdl);
    }

    [[nodiscard]] std::string boundary_identity(
        const SdfAppliedInterconnectTiming& timing,
        const elaboration::BoundaryConversionInfo& conversion,
        const SdfMixedVerilogBoundaryTiming& boundary)
    {
        std::string result = "sdf-mixed-verilog-boundary-v1";
        append_field(result, timing.canonical_identity);
        append_field(result, conversion.path);
        append_field(result, boundary.source_path);
        append_field(result, boundary.destination_path);
        append_field(result, std::to_string(static_cast<unsigned>(boundary.direction)));
        append_field(result, std::to_string(static_cast<unsigned>(conversion.kind)));
        append_field(result, std::to_string(boundary.source_signal));
        append_field(result, std::to_string(boundary.destination_signal));
        append_field(result, std::to_string(boundary.formal_width));
        append_field(result, std::to_string(boundary.actual_width));
        append_field(result,
            std::to_string(static_cast<unsigned>(boundary.source_resolution)));
        append_field(result,
            std::to_string(static_cast<unsigned>(boundary.destination_resolution)));
        for (const auto owner : boundary.source_driver_processes)
            append_field(result, std::to_string(owner));
        for (const auto owner : boundary.destination_load_processes)
            append_field(result, std::to_string(owner));
        return result;
    }

    [[nodiscard]] std::string application_identity(
        const SdfInterconnectTimingApplication& timing,
        const std::vector<SdfMixedVerilogBoundaryTiming>& boundaries)
    {
        std::string result = "sdf-mixed-verilog-application-v1";
        append_field(result, timing.semantic_identity());
        for (const auto& boundary : boundaries)
            append_field(result, boundary.canonical_identity);
        return result;
    }
} // namespace

SdfMixedVerilogApplication::SdfMixedVerilogApplication(
    std::shared_ptr<const SdfInterconnectTimingApplication> timing,
    std::vector<SdfMixedVerilogBoundaryTiming> boundaries,
    std::string semantic_identity)
    : timing_(std::move(timing))
    , boundaries_(std::move(boundaries))
    , semantic_identity_(std::move(semantic_identity))
{
}

[[nodiscard]] bool profiles_match(
    const elaboration::BoundaryConversionInfo& conversion,
    const elaboration::SignalInfo& source,
    const elaboration::SignalInfo& destination)
{
    if (conversion.direction == frontend::PortDirection::Input) {
        return source.width == conversion.actual_width
            && source.source_domain == conversion.actual_domain
            && destination.width == conversion.formal_width
            && destination.source_domain == conversion.formal_domain;
    }
    return source.width == conversion.formal_width
        && source.source_domain == conversion.formal_domain
        && destination.width == conversion.actual_width
        && destination.source_domain == conversion.actual_domain;
}

[[nodiscard]] const elaboration::SignalInfo* find_signal(
    const elaboration::ElaboratedDesign& elaborated, const SignalId signal)
{
    const auto found = std::ranges::find(
        elaborated.signals(), signal, &elaboration::SignalInfo::id);
    return found == elaborated.signals().end() ? nullptr : &*found;
}

const std::shared_ptr<const SdfInterconnectTimingApplication>&
SdfMixedVerilogApplication::timing() const noexcept
{
    return timing_;
}

std::span<const SdfMixedVerilogBoundaryTiming>
SdfMixedVerilogApplication::boundaries() const noexcept
{
    return boundaries_;
}

const SdfMixedVerilogBoundaryTiming*
SdfMixedVerilogApplication::find_boundary(const std::string_view path) const
    noexcept
{
    const auto found = std::ranges::find(
        boundaries_, path, &SdfMixedVerilogBoundaryTiming::boundary_path);
    return found == boundaries_.end() ? nullptr : &*found;
}

std::string_view SdfMixedVerilogApplication::semantic_identity() const noexcept
{
    return semantic_identity_;
}

bool SdfMixedVerilogResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfMixedVerilogResult apply_sdf_mixed_verilog(
    std::shared_ptr<const SdfInterconnectTimingApplication> timing,
    const elaboration::ElaboratedDesign& elaborated,
    const SdfMixedVerilogLimits limits)
{
    SdfMixedVerilogResult result;
    if (!timing || !timing->plan() || timing->semantic_identity().empty()
        || limits.max_boundaries == 0U
        || limits.max_processes_per_boundary == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-MIXED-VERILOG-001",
            "mixed VHDL/Verilog timing requires a complete interconnect application and nonzero limits",
            { });
        return result;
    }

    std::vector<SdfMixedVerilogBoundaryTiming> boundaries;
    std::set<std::string> owners;
    std::size_t identity_bytes { };
    for (const auto& applied : timing->timings()) {
        if (applied.target_kind != SdfTimingTargetKind::Interconnect)
            continue;
        const auto* conversion
            = find_conversion(applied, elaborated, result.diagnostics);
        if (conversion == nullptr) {
            if (std::ranges::any_of(applied.endpoints,
                    [](const auto& endpoint) {
                        return endpoint.conversion.has_value()
                            || endpoint.conversion_peer.has_value();
                    })) {
                diagnose(result.diagnostics, "FSIM-SDF-MIXED-VERILOG-002",
                    "mixed VHDL/Verilog timing has stale or incomplete conversion ownership",
                    applied.annotation_source);
            }
            continue;
        }
        if (boundaries.size() >= limits.max_boundaries) {
            diagnose(result.diagnostics, "FSIM-SDF-MIXED-VERILOG-004",
                "mixed VHDL/Verilog timing exceeds its configured boundary limit",
                applied.annotation_source);
            continue;
        }

        SdfMixedVerilogBoundaryTiming boundary;
        const elaboration::SignalInfo* source_info = nullptr;
        const elaboration::SignalInfo* destination_info = nullptr;
        if (!direction_and_signals(
                *conversion, boundary.source_signal, boundary.destination_signal)
            || (source_info = find_signal(elaborated, boundary.source_signal))
                == nullptr
            || (destination_info
                   = find_signal(elaborated, boundary.destination_signal))
                == nullptr
            || !profiles_match(*conversion, *source_info, *destination_info)
            || !endpoint_languages(applied, boundary.source_signal,
                boundary.destination_signal, boundary.source_language,
                boundary.destination_language)
            || !validate_adapter(*conversion, elaborated,
                boundary.source_signal, boundary.destination_signal)) {
            diagnose(result.diagnostics, "FSIM-SDF-MIXED-VERILOG-003",
                "mixed VHDL/Verilog timing has incompatible direction, language, endpoint, or adapter topology",
                applied.annotation_source);
            continue;
        }
        boundary.direction
            = boundary.source_language == SdfScopeRootLanguage::Vhdl
            ? SdfMixedVerilogDirection::VhdlToVerilog
            : SdfMixedVerilogDirection::VerilogToVhdl;
        boundary.conversion = conversion->kind;
        boundary.boundary_path = conversion->path;
        boundary.target_instance_path = applied.target_instance_path;
        boundary.target_identity = applied.target_identity;
        boundary.source_path
            = find_endpoint(applied, boundary.source_signal)->object_path;
        boundary.destination_path
            = find_endpoint(applied, boundary.destination_signal)->object_path;
        boundary.formal_signal = conversion->formal_signal;
        boundary.actual_signal = conversion->actual_signal;
        boundary.conversion_process = conversion->process;
        boundary.source_driver_processes = process_owners(elaborated,
            boundary.source_signal, process_drives, conversion->process);
        boundary.destination_load_processes = process_owners(elaborated,
            boundary.destination_signal, process_loads, conversion->process);
        if (boundary.source_driver_processes.size()
                > limits.max_processes_per_boundary
            || boundary.destination_load_processes.size()
                > limits.max_processes_per_boundary) {
            diagnose(result.diagnostics, "FSIM-SDF-MIXED-VERILOG-004",
                "mixed VHDL/Verilog timing exceeds its configured driver/load-owner limit",
                applied.annotation_source);
            continue;
        }
        boundary.formal_width = conversion->formal_width;
        boundary.actual_width = conversion->actual_width;
        boundary.formal_domain = conversion->formal_domain;
        boundary.actual_domain = conversion->actual_domain;
        boundary.source_resolution = source_info->resolution;
        boundary.destination_resolution = destination_info->resolution;
        boundary.formal_signed = conversion->formal_signed;
        boundary.actual_signed = conversion->actual_signed;
        boundary.state_domain_changed = conversion->state_domain_changed;
        boundary.transition_delays = applied.transition_delays;
        boundary.connection_source = conversion->connection_span;
        boundary.annotation_source = applied.annotation_source;
        boundary.annotation_identity = applied.annotation_identity;
        boundary.canonical_identity
            = boundary_identity(applied, *conversion, boundary);
        std::string owner = boundary.target_identity;
        append_field(owner, boundary.boundary_path);
        if (!owners.insert(std::move(owner)).second) {
            diagnose(result.diagnostics, "FSIM-SDF-MIXED-VERILOG-002",
                "mixed VHDL/Verilog timing repeats one target and boundary path",
                applied.annotation_source);
            continue;
        }
        if (boundary.canonical_identity.size() > limits.max_identity_bytes
            || identity_bytes > limits.max_identity_bytes
                    - boundary.canonical_identity.size()) {
            diagnose(result.diagnostics, "FSIM-SDF-MIXED-VERILOG-004",
                "mixed VHDL/Verilog boundary identities exceed their configured byte limit",
                applied.annotation_source);
            continue;
        }
        identity_bytes += boundary.canonical_identity.size();
        boundaries.push_back(std::move(boundary));
    }
    if (!result.diagnostics.empty())
        return result;
    const auto identity = application_identity(*timing, boundaries);
    if (identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-MIXED-VERILOG-004",
            "mixed VHDL/Verilog application identity exceeds its configured byte limit",
            { });
        return result;
    }
    result.application = std::make_shared<const SdfMixedVerilogApplication>(
        std::move(timing), std::move(boundaries), identity);
    return result;
}

} // namespace fsim::app
