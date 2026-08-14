// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_vital_path_timing.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfVitalStructuralModelKind {
    Primitive,
    StateTable,
    MemoryPath,
};

struct SdfVitalWrapperPortProfile {
    std::string port_identity;
    runtime::simir::SignalId signal { };
    SdfEndpointRole role { SdfEndpointRole::Input };
    std::size_t width { };
    frontend::PortDirection direction { frontend::PortDirection::Unknown };
    frontend::ValueDomain value_domain { frontend::ValueDomain::Unknown };

    friend bool operator==(const SdfVitalWrapperPortProfile&,
        const SdfVitalWrapperPortProfile&) = default;
};

struct SdfVitalWrapperRegistration {
    std::string target_identity;
    std::string governance_identity;
    std::vector<SdfVitalWrapperPortProfile> ports;

    friend bool operator==(const SdfVitalWrapperRegistration&,
        const SdfVitalWrapperRegistration&) = default;
};

struct SdfVitalModelRecord {
    std::uint64_t node_id { };
    std::uint64_t cell_id { };
    std::string instance_path;
    SdfVitalStructuralModelKind structural_kind {
        SdfVitalStructuralModelKind::Primitive
    };
    bool governed_wrapper { };
    std::string governance_identity;
    std::vector<runtime::simir::ProcessId> owned_processes;
    SdfVitalCallReference call;
    std::string target_identity;
    std::string path_identity;
    frontend::SourceSpan source;
    std::string canonical_identity;

    friend bool operator==(const SdfVitalModelRecord&,
        const SdfVitalModelRecord&) = default;
};

class SdfVitalModelPlan final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    SdfVitalModelPlan(std::shared_ptr<const SdfVitalPathTimingPlan> paths,
        std::vector<SdfVitalWrapperRegistration> wrappers,
        std::vector<SdfVitalModelRecord> records,
        std::string semantic_identity);

    [[nodiscard]] const std::shared_ptr<const SdfVitalPathTimingPlan>& paths()
        const noexcept;
    [[nodiscard]] std::span<const SdfVitalWrapperRegistration> wrappers() const
        noexcept;
    [[nodiscard]] std::span<const SdfVitalModelRecord> records() const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

private:
    std::shared_ptr<const SdfVitalPathTimingPlan> paths_;
    std::vector<SdfVitalWrapperRegistration> wrappers_;
    std::vector<SdfVitalModelRecord> records_;
    std::string semantic_identity_;
};

struct SdfVitalModelLimits {
    std::size_t max_records { 1'000'000U };
    std::size_t max_processes_per_model { 4096U };
    std::size_t max_wrapper_ports { 4096U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfVitalModelResult {
    std::shared_ptr<const SdfVitalModelPlan> plan;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfVitalModelResult build_sdf_vital_model_plan(
    std::shared_ptr<const SdfVitalPathTimingPlan> paths,
    const elaboration::ElaboratedDesign& elaborated,
    std::span<const SdfVitalWrapperRegistration> wrappers = { },
    SdfVitalModelLimits limits = { });

} // namespace fsim::app
