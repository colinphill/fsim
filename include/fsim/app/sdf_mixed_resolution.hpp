// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_mixed_systemc.hpp"
#include "fsim/app/sdf_mixed_systemverilog.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfMixedResolutionKind : std::uint8_t {
    VerilogBoundary,
    SystemVerilogOwnedEndpoint,
    SystemCProxy,
};

struct SdfMixedResolutionSource {
    std::string root_identity;
    std::string library_identity;
    std::shared_ptr<const SdfMixedVerilogApplication> verilog;
    std::shared_ptr<const SdfMixedSystemVerilogApplication> systemverilog;
    std::shared_ptr<const SdfMixedSystemCApplication> systemc;
};

struct SdfMixedResolvedBoundary {
    SdfMixedResolutionKind kind { SdfMixedResolutionKind::VerilogBoundary };
    std::string root_identity;
    std::string library_identity;
    SdfScopeRootLanguage source_language { SdfScopeRootLanguage::Vhdl };
    SdfScopeRootLanguage destination_language {
        SdfScopeRootLanguage::SystemVerilog
    };
    std::string source_path;
    std::string destination_path;
    std::string boundary_path;
    std::string target_identity;
    std::string owner_identity;
    std::string endpoint_identity;
    std::string source_identity;
    std::vector<runtime::SimulationTick> effective_ticks;
    std::string canonical_identity;

    friend bool operator==(const SdfMixedResolvedBoundary&,
        const SdfMixedResolvedBoundary&) = default;
};

class SdfMixedResolutionApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    SdfMixedResolutionApplication(
        std::vector<SdfMixedResolvedBoundary> boundaries,
        std::string semantic_identity);

    [[nodiscard]] std::span<const SdfMixedResolvedBoundary> boundaries() const
        noexcept;
    [[nodiscard]] const SdfMixedResolvedBoundary* find_identity(
        std::string_view identity) const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

private:
    std::vector<SdfMixedResolvedBoundary> boundaries_;
    std::string semantic_identity_;
};

struct SdfMixedResolutionLimits {
    std::size_t max_sources { 4096U };
    std::size_t max_boundaries { 1'000'000U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfMixedResolutionResult {
    std::shared_ptr<const SdfMixedResolutionApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfMixedResolutionResult resolve_sdf_mixed_boundaries(
    std::span<const SdfMixedResolutionSource> sources,
    SdfMixedResolutionLimits limits = { });

} // namespace fsim::app
