// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_mixed_resolution.hpp"

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

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message, const SourceSpan& span = { })
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

    [[nodiscard]] bool valid_direction(const SdfScopeRootLanguage source,
        const SdfScopeRootLanguage destination)
    {
        return source != destination
            && (source == SdfScopeRootLanguage::Vhdl
                || destination == SdfScopeRootLanguage::Vhdl);
    }

    [[nodiscard]] std::string resolved_identity(
        const SdfMixedResolvedBoundary& boundary)
    {
        std::string result = "sdf-mixed-resolution-v1";
        append_field(result, boundary.root_identity);
        append_field(result, boundary.library_identity);
        append_field(result, std::to_string(static_cast<unsigned>(boundary.kind)));
        append_field(result,
            std::to_string(static_cast<unsigned>(boundary.source_language)));
        append_field(result,
            std::to_string(static_cast<unsigned>(boundary.destination_language)));
        append_field(result, boundary.source_path);
        append_field(result, boundary.destination_path);
        append_field(result, boundary.boundary_path);
        append_field(result, boundary.target_identity);
        append_field(result, boundary.owner_identity);
        append_field(result, boundary.endpoint_identity);
        append_field(result, boundary.source_identity);
        for (const auto tick : boundary.effective_ticks)
            append_field(result, std::to_string(tick));
        return result;
    }

    [[nodiscard]] bool add_boundary(std::vector<SdfMixedResolvedBoundary>& result,
        std::set<std::string>& owners, SdfMixedResolvedBoundary boundary,
        const SdfMixedResolutionLimits& limits, std::size_t& identity_bytes,
        std::vector<Diagnostic>& diagnostics)
    {
        if (result.size() >= limits.max_boundaries) {
            diagnose(diagnostics, "FSIM-SDF-MIXED-RESOLUTION-004",
                "mixed resolution exceeds its configured boundary limit");
            return false;
        }
        if (boundary.root_identity.empty() || boundary.library_identity.empty()
            || boundary.source_path.empty() || boundary.destination_path.empty()
            || boundary.source_identity.empty()
            || !valid_direction(
                boundary.source_language, boundary.destination_language)) {
            diagnose(diagnostics, "FSIM-SDF-MIXED-RESOLUTION-003",
                "mixed resolution source has an incomplete path or invalid language direction");
            return false;
        }
        std::string owner = boundary.root_identity;
        append_field(owner, boundary.library_identity);
        append_field(owner,
            std::to_string(static_cast<unsigned>(boundary.source_language)));
        append_field(owner,
            std::to_string(static_cast<unsigned>(boundary.destination_language)));
        append_field(owner, boundary.source_path);
        append_field(owner, boundary.destination_path);
        append_field(owner, boundary.owner_identity);
        append_field(owner, boundary.endpoint_identity);
        if (!owners.insert(std::move(owner)).second) {
            diagnose(diagnostics, "FSIM-SDF-MIXED-RESOLUTION-002",
                "mixed resolution has an ambiguous duplicate direction-aware path");
            return false;
        }
        boundary.canonical_identity = resolved_identity(boundary);
        if (boundary.canonical_identity.size() > limits.max_identity_bytes
            || identity_bytes
                > limits.max_identity_bytes - boundary.canonical_identity.size()) {
            diagnose(diagnostics, "FSIM-SDF-MIXED-RESOLUTION-004",
                "mixed resolution identities exceed their configured byte limit");
            return false;
        }
        identity_bytes += boundary.canonical_identity.size();
        result.push_back(std::move(boundary));
        return true;
    }

    void append_verilog(const SdfMixedResolutionSource& source,
        std::vector<SdfMixedResolvedBoundary>& boundaries,
        std::set<std::string>& owners, const SdfMixedResolutionLimits& limits,
        std::size_t& identity_bytes, std::vector<Diagnostic>& diagnostics)
    {
        if (!source.verilog)
            return;
        for (const auto& input : source.verilog->boundaries()) {
            SdfMixedResolvedBoundary boundary;
            boundary.kind = SdfMixedResolutionKind::VerilogBoundary;
            boundary.root_identity = source.root_identity;
            boundary.library_identity = source.library_identity;
            boundary.source_language = input.source_language;
            boundary.destination_language = input.destination_language;
            boundary.source_path = input.source_path;
            boundary.destination_path = input.destination_path;
            boundary.boundary_path = input.boundary_path;
            boundary.target_identity = input.target_identity;
            boundary.source_identity = input.canonical_identity;
            boundary.effective_ticks = input.transition_delays;
            (void)add_boundary(boundaries, owners, std::move(boundary), limits,
                identity_bytes, diagnostics);
        }
    }

    void append_systemverilog(const SdfMixedResolutionSource& source,
        std::vector<SdfMixedResolvedBoundary>& boundaries,
        std::set<std::string>& owners, const SdfMixedResolutionLimits& limits,
        std::size_t& identity_bytes, std::vector<Diagnostic>& diagnostics)
    {
        if (!source.systemverilog)
            return;
        const auto& mixed = source.systemverilog->mixed();
        if (!mixed) {
            diagnose(diagnostics, "FSIM-SDF-MIXED-RESOLUTION-001",
                "mixed SystemVerilog resolution source has no base boundary application");
            return;
        }
        for (const auto& endpoint : source.systemverilog->endpoints()) {
            const auto* input = mixed->find_boundary(endpoint.boundary_path);
            if (input == nullptr) {
                diagnose(diagnostics, "FSIM-SDF-MIXED-RESOLUTION-003",
                    "mixed SystemVerilog endpoint lost its base boundary path");
                continue;
            }
            SdfMixedResolvedBoundary boundary;
            boundary.kind = SdfMixedResolutionKind::SystemVerilogOwnedEndpoint;
            boundary.root_identity = source.root_identity;
            boundary.library_identity = source.library_identity;
            boundary.source_language = input->source_language;
            boundary.destination_language = input->destination_language;
            boundary.source_path = input->source_path;
            boundary.destination_path = input->destination_path;
            boundary.boundary_path = input->boundary_path;
            boundary.target_identity = input->target_identity;
            boundary.owner_identity = endpoint.owner_identity;
            boundary.endpoint_identity = endpoint.endpoint_identity;
            boundary.source_identity = endpoint.canonical_identity;
            boundary.effective_ticks = input->transition_delays;
            (void)add_boundary(boundaries, owners, std::move(boundary), limits,
                identity_bytes, diagnostics);
        }
    }

    void append_systemc(const SdfMixedResolutionSource& source,
        std::vector<SdfMixedResolvedBoundary>& boundaries,
        std::set<std::string>& owners, const SdfMixedResolutionLimits& limits,
        std::size_t& identity_bytes, std::vector<Diagnostic>& diagnostics)
    {
        if (!source.systemc)
            return;
        for (const auto& proxy : source.systemc->proxies()) {
            SdfMixedResolvedBoundary boundary;
            boundary.kind = SdfMixedResolutionKind::SystemCProxy;
            boundary.root_identity = source.root_identity;
            boundary.library_identity = source.library_identity;
            boundary.source_language = proxy.source_language;
            boundary.destination_language = proxy.destination_language;
            boundary.source_path = proxy.source_language
                    == SdfScopeRootLanguage::SystemC
                ? proxy.object_path
                : proxy.target_instance_path;
            boundary.destination_path = proxy.destination_language
                    == SdfScopeRootLanguage::SystemC
                ? proxy.object_path
                : proxy.target_instance_path;
            boundary.boundary_path = proxy.object_path;
            boundary.target_identity = proxy.target_identity;
            boundary.owner_identity = proxy.parent_path;
            boundary.endpoint_identity = proxy.object_path;
            boundary.source_identity = proxy.canonical_identity;
            boundary.effective_ticks = proxy.transition_delays;
            (void)add_boundary(boundaries, owners, std::move(boundary), limits,
                identity_bytes, diagnostics);
        }
    }

    [[nodiscard]] std::string application_identity(
        const std::vector<SdfMixedResolvedBoundary>& boundaries)
    {
        std::string result = "sdf-mixed-resolution-application-v1";
        for (const auto& boundary : boundaries)
            append_field(result, boundary.canonical_identity);
        return result;
    }
} // namespace

SdfMixedResolutionApplication::SdfMixedResolutionApplication(
    std::vector<SdfMixedResolvedBoundary> boundaries,
    std::string semantic_identity)
    : boundaries_(std::move(boundaries))
    , semantic_identity_(std::move(semantic_identity))
{
}

std::span<const SdfMixedResolvedBoundary>
SdfMixedResolutionApplication::boundaries() const noexcept
{
    return boundaries_;
}

const SdfMixedResolvedBoundary* SdfMixedResolutionApplication::find_identity(
    const std::string_view identity) const noexcept
{
    const auto found = std::ranges::find(
        boundaries_, identity, &SdfMixedResolvedBoundary::canonical_identity);
    return found == boundaries_.end() ? nullptr : &*found;
}

std::string_view SdfMixedResolutionApplication::semantic_identity() const
    noexcept
{
    return semantic_identity_;
}

bool SdfMixedResolutionResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfMixedResolutionResult resolve_sdf_mixed_boundaries(
    const std::span<const SdfMixedResolutionSource> sources,
    const SdfMixedResolutionLimits limits)
{
    SdfMixedResolutionResult result;
    if (sources.empty() || sources.size() > limits.max_sources
        || limits.max_sources == 0U || limits.max_boundaries == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-MIXED-RESOLUTION-001",
            "mixed resolution requires sources within nonzero resource limits");
        return result;
    }
    std::vector<SdfMixedResolvedBoundary> boundaries;
    std::set<std::string> owners;
    std::size_t identity_bytes { };
    for (const auto& source : sources) {
        if (!source.verilog && !source.systemverilog && !source.systemc) {
            diagnose(result.diagnostics, "FSIM-SDF-MIXED-RESOLUTION-001",
                "mixed resolution source has no boundary application");
            continue;
        }
        append_verilog(source, boundaries, owners, limits, identity_bytes,
            result.diagnostics);
        append_systemverilog(source, boundaries, owners, limits, identity_bytes,
            result.diagnostics);
        append_systemc(source, boundaries, owners, limits, identity_bytes,
            result.diagnostics);
    }
    if (!result.diagnostics.empty())
        return result;
    std::ranges::sort(boundaries, { },
        &SdfMixedResolvedBoundary::canonical_identity);
    const auto identity = application_identity(boundaries);
    if (identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-MIXED-RESOLUTION-004",
            "mixed resolution application identity exceeds its configured byte limit");
        return result;
    }
    result.application = std::make_shared<const SdfMixedResolutionApplication>(
        std::move(boundaries), identity);
    return result;
}

} // namespace fsim::app
