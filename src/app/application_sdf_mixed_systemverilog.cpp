// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_mixed_systemverilog.hpp"

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

    [[nodiscard]] std::string unit_identity(const semantic::sv::Unit& unit)
    {
        return unit.library + "." + unit.name;
    }

    [[nodiscard]] std::optional<semantic::sv::UnitKind> unit_kind(
        const SdfMixedSystemVerilogOwnerKind kind)
    {
        if (kind == SdfMixedSystemVerilogOwnerKind::Interface)
            return semantic::sv::UnitKind::interface;
        if (kind == SdfMixedSystemVerilogOwnerKind::Program)
            return semantic::sv::UnitKind::program;
        if (kind == SdfMixedSystemVerilogOwnerKind::Package)
            return semantic::sv::UnitKind::package;
        return std::nullopt;
    }

    [[nodiscard]] const semantic::sv::Unit* find_unit(
        const semantic::sv::Hir& hir, const std::string_view identity,
        const semantic::sv::UnitKind kind)
    {
        const semantic::sv::Unit* result = nullptr;
        for (const auto& unit : hir.units()) {
            if (unit.kind != kind || unit_identity(unit) != identity)
                continue;
            if (result != nullptr)
                return nullptr;
            result = &unit;
        }
        return result;
    }

    [[nodiscard]] const semantic::sv::ClassDeclaration* find_class(
        const semantic::sv::Hir& hir, const std::string_view identity)
    {
        const semantic::sv::ClassDeclaration* result = nullptr;
        for (const auto& declaration : hir.classes()) {
            if (declaration.canonical_identity != identity)
                continue;
            if (result != nullptr)
                return nullptr;
            result = &declaration;
        }
        return result;
    }

    struct AssertionOwner {
        const semantic::sv::Unit* unit { };
        const semantic::sv::ConcurrentAssertion* assertion { };
    };

    [[nodiscard]] AssertionOwner find_assertion(
        const semantic::sv::Hir& hir, const std::string_view identity)
    {
        AssertionOwner result;
        for (const auto& unit : hir.units()) {
            for (const auto& assertion : unit.concurrent_assertions) {
                const auto candidate = unit_identity(unit) + "." + assertion.name;
                if (candidate != identity)
                    continue;
                if (result.assertion != nullptr)
                    return { };
                result = { &unit, &assertion };
            }
        }
        return result;
    }

    [[nodiscard]] SdfMixedSystemVerilogRegion assertion_region(
        const semantic::sv::AssertionRegion region)
    {
        if (region == semantic::sv::AssertionRegion::preponed)
            return SdfMixedSystemVerilogRegion::Preponed;
        if (region == semantic::sv::AssertionRegion::observed)
            return SdfMixedSystemVerilogRegion::Observed;
        return SdfMixedSystemVerilogRegion::Reactive;
    }

    [[nodiscard]] const SdfMixedVerilogBoundaryTiming* find_boundary(
        const SdfMixedVerilogApplication& mixed, const std::string_view path)
    {
        return mixed.find_boundary(path);
    }

    [[nodiscard]] bool systemverilog_signal_profile(
        const SdfMixedVerilogBoundaryTiming& boundary,
        const SdfMixedSystemVerilogBinding& binding)
    {
        if (boundary.source_language == SdfScopeRootLanguage::SystemVerilog
            || boundary.source_language == SdfScopeRootLanguage::Verilog) {
            if (binding.signal != boundary.source_signal)
                return false;
        } else if (binding.signal != boundary.destination_signal) {
            return false;
        }
        if (binding.signal == boundary.formal_signal) {
            return binding.width == boundary.formal_width
                && binding.domain == boundary.formal_domain;
        }
        return binding.signal == boundary.actual_signal
            && binding.width == boundary.actual_width
            && binding.domain == boundary.actual_domain;
    }

    [[nodiscard]] bool expected_region(
        const SdfMixedSystemVerilogBinding& binding)
    {
        if (binding.owner_kind == SdfMixedSystemVerilogOwnerKind::Program)
            return binding.event_region == SdfMixedSystemVerilogRegion::Reactive;
        if (binding.owner_kind == SdfMixedSystemVerilogOwnerKind::Assertion)
            return binding.event_region == SdfMixedSystemVerilogRegion::Observed;
        return binding.event_region == SdfMixedSystemVerilogRegion::Active;
    }

    [[nodiscard]] bool resolve_owner(const semantic::sv::Hir& hir,
        const SdfMixedSystemVerilogBinding& binding,
        SdfMixedSystemVerilogEndpoint& endpoint)
    {
        endpoint.sampling_region = binding.event_region;
        endpoint.evaluation_region = binding.event_region;
        endpoint.action_region = binding.event_region;
        if (const auto kind = unit_kind(binding.owner_kind))
            return find_unit(hir, binding.owner_identity, *kind) != nullptr;
        if (binding.owner_kind == SdfMixedSystemVerilogOwnerKind::Class)
            return find_class(hir, binding.owner_identity) != nullptr;
        const auto owner = find_assertion(hir, binding.owner_identity);
        if (owner.assertion == nullptr)
            return false;
        endpoint.sampling_region
            = assertion_region(owner.assertion->sampling_region);
        endpoint.evaluation_region
            = assertion_region(owner.assertion->evaluation_region);
        endpoint.action_region = assertion_region(owner.assertion->action_region);
        return endpoint.sampling_region == SdfMixedSystemVerilogRegion::Preponed
            && endpoint.evaluation_region == SdfMixedSystemVerilogRegion::Observed
            && endpoint.action_region == SdfMixedSystemVerilogRegion::Reactive;
    }

    [[nodiscard]] std::string endpoint_identity(
        const SdfMixedSystemVerilogBinding& binding,
        const SdfMixedVerilogBoundaryTiming& boundary,
        const SdfMixedSystemVerilogEndpoint& endpoint)
    {
        std::string result = "sdf-mixed-systemverilog-endpoint-v1";
        append_field(result, boundary.canonical_identity);
        append_field(result, binding.owner_identity);
        append_field(result, binding.endpoint_identity);
        append_field(result, std::to_string(static_cast<unsigned>(binding.owner_kind)));
        append_field(result, std::to_string(binding.signal));
        append_field(result, std::to_string(binding.width));
        append_field(result, std::to_string(static_cast<unsigned>(binding.domain)));
        append_field(result, std::to_string(static_cast<unsigned>(endpoint.event_region)));
        append_field(result, std::to_string(static_cast<unsigned>(endpoint.sampling_region)));
        append_field(result, std::to_string(static_cast<unsigned>(endpoint.evaluation_region)));
        append_field(result, std::to_string(static_cast<unsigned>(endpoint.action_region)));
        return result;
    }

    [[nodiscard]] std::string application_identity(
        const SdfMixedVerilogApplication& mixed,
        const std::vector<SdfMixedSystemVerilogEndpoint>& endpoints)
    {
        std::string result = "sdf-mixed-systemverilog-application-v1";
        append_field(result, mixed.semantic_identity());
        for (const auto& endpoint : endpoints)
            append_field(result, endpoint.canonical_identity);
        return result;
    }
} // namespace

SdfMixedSystemVerilogApplication::SdfMixedSystemVerilogApplication(
    std::shared_ptr<const SdfMixedVerilogApplication> mixed,
    std::vector<SdfMixedSystemVerilogEndpoint> endpoints,
    std::string semantic_identity)
    : mixed_(std::move(mixed))
    , endpoints_(std::move(endpoints))
    , semantic_identity_(std::move(semantic_identity))
{
}

const std::shared_ptr<const SdfMixedVerilogApplication>&
SdfMixedSystemVerilogApplication::mixed() const noexcept
{
    return mixed_;
}

std::span<const SdfMixedSystemVerilogEndpoint>
SdfMixedSystemVerilogApplication::endpoints() const noexcept
{
    return endpoints_;
}

std::string_view SdfMixedSystemVerilogApplication::semantic_identity() const
    noexcept
{
    return semantic_identity_;
}

bool SdfMixedSystemVerilogResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfMixedSystemVerilogResult apply_sdf_mixed_systemverilog(
    std::shared_ptr<const SdfMixedVerilogApplication> mixed,
    const semantic::sv::Hir& hir,
    const std::span<const SdfMixedSystemVerilogBinding> bindings,
    const SdfMixedSystemVerilogLimits limits)
{
    SdfMixedSystemVerilogResult result;
    if (!mixed || !mixed->timing() || mixed->semantic_identity().empty()
        || bindings.empty() || limits.max_bindings == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-MIXED-SYSTEMVERILOG-001",
            "mixed SystemVerilog timing requires a complete mixed application, bindings, and nonzero limits",
            { });
        return result;
    }

    std::vector<SdfMixedSystemVerilogEndpoint> endpoints;
    std::set<std::string> owners;
    std::size_t identity_bytes { };
    for (const auto& binding : bindings) {
        if (endpoints.size() >= limits.max_bindings) {
            diagnose(result.diagnostics, "FSIM-SDF-MIXED-SYSTEMVERILOG-004",
                "mixed SystemVerilog timing exceeds its configured binding limit",
                binding.source);
            continue;
        }
        const auto* boundary = find_boundary(*mixed, binding.boundary_path);
        if (boundary == nullptr || binding.owner_identity.empty()
            || binding.endpoint_identity.empty()) {
            diagnose(result.diagnostics, "FSIM-SDF-MIXED-SYSTEMVERILOG-002",
                "mixed SystemVerilog timing has a stale boundary or missing owner identity",
                binding.source);
            continue;
        }
        SdfMixedSystemVerilogEndpoint endpoint;
        endpoint.boundary_path = binding.boundary_path;
        endpoint.owner_kind = binding.owner_kind;
        endpoint.owner_identity = binding.owner_identity;
        endpoint.endpoint_identity = binding.endpoint_identity;
        endpoint.signal = binding.signal;
        endpoint.width = binding.width;
        endpoint.domain = binding.domain;
        endpoint.event_region = binding.event_region;
        endpoint.source = binding.source;
        if (!systemverilog_signal_profile(*boundary, binding)
            || !expected_region(binding)
            || !resolve_owner(hir, binding, endpoint)) {
            diagnose(result.diagnostics, "FSIM-SDF-MIXED-SYSTEMVERILOG-003",
                "mixed SystemVerilog endpoint widens its signal, changes its event region, or lacks matching HIR ownership",
                binding.source);
            continue;
        }
        std::string owner = binding.boundary_path;
        append_field(owner, binding.owner_identity);
        append_field(owner, binding.endpoint_identity);
        append_field(owner, std::to_string(static_cast<unsigned>(binding.owner_kind)));
        if (!owners.insert(std::move(owner)).second) {
            diagnose(result.diagnostics, "FSIM-SDF-MIXED-SYSTEMVERILOG-002",
                "mixed SystemVerilog timing repeats one owned endpoint",
                binding.source);
            continue;
        }
        endpoint.canonical_identity
            = endpoint_identity(binding, *boundary, endpoint);
        if (endpoint.canonical_identity.size() > limits.max_identity_bytes
            || identity_bytes > limits.max_identity_bytes
                    - endpoint.canonical_identity.size()) {
            diagnose(result.diagnostics, "FSIM-SDF-MIXED-SYSTEMVERILOG-004",
                "mixed SystemVerilog endpoint identities exceed their configured byte limit",
                binding.source);
            continue;
        }
        identity_bytes += endpoint.canonical_identity.size();
        endpoints.push_back(std::move(endpoint));
    }
    if (!result.diagnostics.empty())
        return result;
    const auto identity = application_identity(*mixed, endpoints);
    if (identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-MIXED-SYSTEMVERILOG-004",
            "mixed SystemVerilog application identity exceeds its configured byte limit",
            { });
        return result;
    }
    result.application
        = std::make_shared<const SdfMixedSystemVerilogApplication>(
            std::move(mixed), std::move(endpoints), identity);
    return result;
}

} // namespace fsim::app
