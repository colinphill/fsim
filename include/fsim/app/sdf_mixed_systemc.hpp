// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_interconnect_timing.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

struct SdfMixedSystemCSample {
    runtime::SimulationTick tick { };
    std::uint64_t delta { };
    runtime::PackedLogic9 value;

    friend bool operator==(const SdfMixedSystemCSample&,
        const SdfMixedSystemCSample&) = default;
};

struct SdfMixedSystemCBinding {
    std::string target_identity;
    std::string object_path;
    std::vector<SdfMixedSystemCSample> samples;
    frontend::SourceSpan source;
};

struct SdfMixedSystemCProxyTiming {
    std::string target_identity;
    std::string target_instance_path;
    std::string object_path;
    std::string parent_path;
    std::string type_name;
    SdfScopeRootLanguage source_language { SdfScopeRootLanguage::Vhdl };
    SdfScopeRootLanguage destination_language { SdfScopeRootLanguage::SystemC };
    elaboration::SystemCNamedObjectKind object_kind {
        elaboration::SystemCNamedObjectKind::port
    };
    std::uint64_t native_handle { };
    runtime::simir::SignalId signal { };
    std::size_t width { };
    frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
    runtime::simir::ResolutionKind resolution {
        runtime::simir::ResolutionKind::none
    };
    std::vector<runtime::SimulationTick> transition_delays;
    std::vector<SdfMixedSystemCSample> samples;
    frontend::SourceSpan annotation_source;
    std::string annotation_identity;
    std::string canonical_identity;

    friend bool operator==(const SdfMixedSystemCProxyTiming&,
        const SdfMixedSystemCProxyTiming&) = default;
};

class SdfMixedSystemCApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    SdfMixedSystemCApplication(
        std::shared_ptr<const SdfInterconnectTimingApplication> timing,
        std::vector<SdfMixedSystemCProxyTiming> proxies,
        std::string semantic_identity);

    [[nodiscard]] const std::shared_ptr<const SdfInterconnectTimingApplication>&
    timing() const noexcept;
    [[nodiscard]] std::span<const SdfMixedSystemCProxyTiming> proxies() const
        noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

private:
    std::shared_ptr<const SdfInterconnectTimingApplication> timing_;
    std::vector<SdfMixedSystemCProxyTiming> proxies_;
    std::string semantic_identity_;
};

struct SdfMixedSystemCLimits {
    std::size_t max_proxies { 1'000'000U };
    std::size_t max_samples_per_proxy { 1'000'000U };
    std::size_t max_value_bits { 1U << 24U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfMixedSystemCResult {
    std::shared_ptr<const SdfMixedSystemCApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfMixedSystemCResult apply_sdf_mixed_systemc(
    std::shared_ptr<const SdfInterconnectTimingApplication> timing,
    const elaboration::ElaboratedDesign& elaborated,
    std::span<const SdfMixedSystemCBinding> bindings,
    SdfMixedSystemCLimits limits = { });

} // namespace fsim::app
