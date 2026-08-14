// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_interconnect_timing.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfMixedVerilogDirection : std::uint8_t {
    VhdlToVerilog,
    VerilogToVhdl,
};

struct SdfMixedVerilogBoundaryTiming {
    SdfMixedVerilogDirection direction {
        SdfMixedVerilogDirection::VerilogToVhdl
    };
    elaboration::BoundaryConversionKind conversion {
        elaboration::BoundaryConversionKind::ordinal_alias
    };
    std::string boundary_path;
    std::string target_instance_path;
    std::string target_identity;
    std::string source_path;
    std::string destination_path;
    SdfScopeRootLanguage source_language { SdfScopeRootLanguage::Vhdl };
    SdfScopeRootLanguage destination_language { SdfScopeRootLanguage::Verilog };
    runtime::simir::SignalId source_signal { };
    runtime::simir::SignalId destination_signal { };
    runtime::simir::SignalId formal_signal { };
    runtime::simir::SignalId actual_signal { };
    std::optional<runtime::simir::ProcessId> conversion_process;
    std::vector<runtime::simir::ProcessId> source_driver_processes;
    std::vector<runtime::simir::ProcessId> destination_load_processes;
    std::size_t formal_width { };
    std::size_t actual_width { };
    frontend::ValueDomain formal_domain { frontend::ValueDomain::Unknown };
    frontend::ValueDomain actual_domain { frontend::ValueDomain::Unknown };
    runtime::simir::ResolutionKind source_resolution {
        runtime::simir::ResolutionKind::none
    };
    runtime::simir::ResolutionKind destination_resolution {
        runtime::simir::ResolutionKind::none
    };
    bool formal_signed { };
    bool actual_signed { };
    bool state_domain_changed { };
    std::vector<runtime::SimulationTick> transition_delays;
    frontend::SourceSpan connection_source;
    frontend::SourceSpan annotation_source;
    std::string annotation_identity;
    std::string canonical_identity;

    friend bool operator==(const SdfMixedVerilogBoundaryTiming&,
        const SdfMixedVerilogBoundaryTiming&) = default;
};

class SdfMixedVerilogApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    SdfMixedVerilogApplication(
        std::shared_ptr<const SdfInterconnectTimingApplication> timing,
        std::vector<SdfMixedVerilogBoundaryTiming> boundaries,
        std::string semantic_identity);

    [[nodiscard]] const std::shared_ptr<const SdfInterconnectTimingApplication>&
    timing() const noexcept;
    [[nodiscard]] std::span<const SdfMixedVerilogBoundaryTiming> boundaries()
        const noexcept;
    [[nodiscard]] const SdfMixedVerilogBoundaryTiming* find_boundary(
        std::string_view path) const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

private:
    std::shared_ptr<const SdfInterconnectTimingApplication> timing_;
    std::vector<SdfMixedVerilogBoundaryTiming> boundaries_;
    std::string semantic_identity_;
};

struct SdfMixedVerilogLimits {
    std::size_t max_boundaries { 1'000'000U };
    std::size_t max_processes_per_boundary { 1'000'000U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfMixedVerilogResult {
    std::shared_ptr<const SdfMixedVerilogApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfMixedVerilogResult apply_sdf_mixed_verilog(
    std::shared_ptr<const SdfInterconnectTimingApplication> timing,
    const elaboration::ElaboratedDesign& elaborated,
    SdfMixedVerilogLimits limits = { });

} // namespace fsim::app
