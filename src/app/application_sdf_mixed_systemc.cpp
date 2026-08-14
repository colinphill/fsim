// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_mixed_systemc.hpp"

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

    [[nodiscard]] const SdfAppliedInterconnectTiming* find_timing(
        const SdfInterconnectTimingApplication& timing,
        const std::string_view identity)
    {
        return timing.find_target(identity);
    }

    [[nodiscard]] const elaboration::SystemCNamedObjectInfo* find_object(
        const elaboration::ElaboratedDesign& elaborated,
        const std::string_view path)
    {
        const elaboration::SystemCNamedObjectInfo* result = nullptr;
        for (const auto& object : elaborated.systemc_objects()) {
            if (object.name != path)
                continue;
            if (result != nullptr)
                return nullptr;
            result = &object;
        }
        return result;
    }

    [[nodiscard]] const elaboration::SignalInfo* find_signal(
        const elaboration::ElaboratedDesign& elaborated,
        const runtime::simir::SignalId signal)
    {
        const auto found = std::ranges::find(
            elaborated.signals(), signal, &elaboration::SignalInfo::id);
        return found == elaborated.signals().end() ? nullptr : &*found;
    }

    [[nodiscard]] const SdfResolvedEndpoint* find_endpoint(
        const SdfAppliedInterconnectTiming& timing, const std::string_view path,
        const runtime::simir::SignalId signal)
    {
        const auto found = std::ranges::find_if(timing.endpoints,
            [&](const auto& endpoint) {
                return endpoint.language == SdfScopeRootLanguage::SystemC
                    && endpoint.object_path == path && endpoint.signal == signal
                    && (endpoint.object_kind == SdfEndpointObjectKind::SystemCPort
                        || endpoint.object_kind
                            == SdfEndpointObjectKind::SystemCSignal);
            });
        return found == timing.endpoints.end() ? nullptr : &*found;
    }

    [[nodiscard]] bool supported_object(
        const elaboration::SystemCNamedObjectInfo& object)
    {
        return object.signal
            && (object.kind == elaboration::SystemCNamedObjectKind::port
                || object.kind == elaboration::SystemCNamedObjectKind::signal
                || object.kind
                    == elaboration::SystemCNamedObjectKind::export_object);
    }

    [[nodiscard]] bool ordered_samples(
        const std::span<const SdfMixedSystemCSample> samples,
        const std::size_t width)
    {
        if (samples.empty())
            return false;
        for (std::size_t index = 0; index < samples.size(); ++index) {
            if (samples[index].value.width() != width) {
                return false;
            }
            if (index != 0U
                && (samples[index].tick < samples[index - 1U].tick
                    || (samples[index].tick == samples[index - 1U].tick
                        && samples[index].delta
                            <= samples[index - 1U].delta))) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool sample_resources(
        const std::span<const SdfMixedSystemCSample> samples,
        const std::size_t width, const SdfMixedSystemCLimits& limits)
    {
        return samples.size() <= limits.max_samples_per_proxy
            && width <= limits.max_value_bits
            && (samples.empty()
                || width <= limits.max_value_bits / samples.size());
    }

    [[nodiscard]] std::string proxy_identity(
        const SdfAppliedInterconnectTiming& timing,
        const SdfMixedSystemCProxyTiming& proxy)
    {
        std::string result = "sdf-mixed-systemc-proxy-v1";
        append_field(result, timing.canonical_identity);
        append_field(result, proxy.object_path);
        append_field(result, proxy.parent_path);
        append_field(result, proxy.type_name);
        append_field(result,
            std::to_string(static_cast<unsigned>(proxy.source_language)));
        append_field(result,
            std::to_string(static_cast<unsigned>(proxy.destination_language)));
        append_field(result, std::to_string(proxy.native_handle));
        append_field(result, std::to_string(proxy.signal));
        append_field(result, std::to_string(proxy.width));
        append_field(result, std::to_string(static_cast<unsigned>(proxy.domain)));
        for (const auto& sample : proxy.samples) {
            append_field(result, std::to_string(sample.tick));
            append_field(result, std::to_string(sample.delta));
            append_field(result, sample.value.to_msb_string());
        }
        return result;
    }

    [[nodiscard]] std::string application_identity(
        const SdfInterconnectTimingApplication& timing,
        const std::vector<SdfMixedSystemCProxyTiming>& proxies)
    {
        std::string result = "sdf-mixed-systemc-application-v1";
        append_field(result, timing.semantic_identity());
        for (const auto& proxy : proxies)
            append_field(result, proxy.canonical_identity);
        return result;
    }
} // namespace

SdfMixedSystemCApplication::SdfMixedSystemCApplication(
    std::shared_ptr<const SdfInterconnectTimingApplication> timing,
    std::vector<SdfMixedSystemCProxyTiming> proxies,
    std::string semantic_identity)
    : timing_(std::move(timing))
    , proxies_(std::move(proxies))
    , semantic_identity_(std::move(semantic_identity))
{
}

const std::shared_ptr<const SdfInterconnectTimingApplication>&
SdfMixedSystemCApplication::timing() const noexcept
{
    return timing_;
}

std::span<const SdfMixedSystemCProxyTiming>
SdfMixedSystemCApplication::proxies() const noexcept
{
    return proxies_;
}

std::string_view SdfMixedSystemCApplication::semantic_identity() const noexcept
{
    return semantic_identity_;
}

bool SdfMixedSystemCResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfMixedSystemCResult apply_sdf_mixed_systemc(
    std::shared_ptr<const SdfInterconnectTimingApplication> timing,
    const elaboration::ElaboratedDesign& elaborated,
    const std::span<const SdfMixedSystemCBinding> bindings,
    const SdfMixedSystemCLimits limits)
{
    SdfMixedSystemCResult result;
    if (!timing || timing->semantic_identity().empty() || bindings.empty()
        || limits.max_proxies == 0U || limits.max_samples_per_proxy == 0U
        || limits.max_value_bits == 0U || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-MIXED-SYSTEMC-001",
            "mixed SystemC timing requires a complete timing application, bindings, and nonzero limits",
            { });
        return result;
    }

    std::vector<SdfMixedSystemCProxyTiming> proxies;
    std::set<std::string> owners;
    std::size_t identity_bytes { };
    for (const auto& binding : bindings) {
        if (proxies.size() >= limits.max_proxies) {
            diagnose(result.diagnostics, "FSIM-SDF-MIXED-SYSTEMC-004",
                "mixed SystemC timing exceeds its configured proxy limit",
                binding.source);
            continue;
        }
        const auto* applied = find_timing(*timing, binding.target_identity);
        const auto* object = find_object(elaborated, binding.object_path);
        if (applied == nullptr || object == nullptr) {
            diagnose(result.diagnostics, "FSIM-SDF-MIXED-SYSTEMC-002",
                "mixed SystemC timing has a stale target or ambiguous object path",
                binding.source);
            continue;
        }
        if (!supported_object(*object)) {
            diagnose(result.diagnostics, "FSIM-SDF-MIXED-SYSTEMC-003",
                "custom SystemC channel has no supported typed signal adapter",
                binding.source);
            continue;
        }
        const auto* signal = find_signal(elaborated, *object->signal);
        const auto* endpoint
            = find_endpoint(*applied, binding.object_path, *object->signal);
        if (signal != nullptr
            && !sample_resources(binding.samples, signal->width, limits)) {
            diagnose(result.diagnostics, "FSIM-SDF-MIXED-SYSTEMC-004",
                "mixed SystemC samples exceed their configured count or value-bit limit",
                binding.source);
            continue;
        }
        if (signal == nullptr || endpoint == nullptr
            || !ordered_samples(binding.samples, signal->width)) {
            diagnose(result.diagnostics, "FSIM-SDF-MIXED-SYSTEMC-003",
                "mixed SystemC proxy has an incompatible endpoint, value width, or time/delta sequence",
                binding.source);
            continue;
        }
        std::string owner = binding.target_identity;
        append_field(owner, binding.object_path);
        if (!owners.insert(std::move(owner)).second) {
            diagnose(result.diagnostics, "FSIM-SDF-MIXED-SYSTEMC-002",
                "mixed SystemC timing repeats one target and object path",
                binding.source);
            continue;
        }
        SdfMixedSystemCProxyTiming proxy;
        proxy.target_identity = applied->target_identity;
        proxy.target_instance_path = applied->target_instance_path;
        proxy.object_path = object->name;
        proxy.parent_path = object->parent;
        proxy.type_name = object->type_name;
        if (endpoint->role == SdfEndpointRole::InterconnectSource) {
            proxy.source_language = SdfScopeRootLanguage::SystemC;
            proxy.destination_language = SdfScopeRootLanguage::Vhdl;
        } else {
            proxy.source_language = SdfScopeRootLanguage::Vhdl;
            proxy.destination_language = SdfScopeRootLanguage::SystemC;
        }
        proxy.object_kind = object->kind;
        proxy.native_handle = object->native_handle;
        proxy.signal = *object->signal;
        proxy.width = signal->width;
        proxy.domain = signal->source_domain;
        proxy.resolution = signal->resolution;
        proxy.transition_delays = applied->transition_delays;
        proxy.samples = binding.samples;
        proxy.annotation_source = applied->annotation_source;
        proxy.annotation_identity = applied->annotation_identity;
        proxy.canonical_identity = proxy_identity(*applied, proxy);
        if (proxy.canonical_identity.size() > limits.max_identity_bytes
            || identity_bytes > limits.max_identity_bytes
                    - proxy.canonical_identity.size()) {
            diagnose(result.diagnostics, "FSIM-SDF-MIXED-SYSTEMC-004",
                "mixed SystemC proxy identities exceed their configured byte limit",
                binding.source);
            continue;
        }
        identity_bytes += proxy.canonical_identity.size();
        proxies.push_back(std::move(proxy));
    }
    if (!result.diagnostics.empty())
        return result;
    const auto identity = application_identity(*timing, proxies);
    if (identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-MIXED-SYSTEMC-004",
            "mixed SystemC application identity exceeds its configured byte limit",
            { });
        return result;
    }
    result.application = std::make_shared<const SdfMixedSystemCApplication>(
        std::move(timing), std::move(proxies), identity);
    return result;
}

} // namespace fsim::app
