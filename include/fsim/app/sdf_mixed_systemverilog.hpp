// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_mixed_verilog.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfMixedSystemVerilogOwnerKind : std::uint8_t {
    Interface,
    Program,
    Class,
    Assertion,
    Package,
};

enum class SdfMixedSystemVerilogRegion : std::uint8_t {
    Active,
    Preponed,
    Observed,
    Reactive,
};

struct SdfMixedSystemVerilogBinding {
    std::string boundary_path;
    SdfMixedSystemVerilogOwnerKind owner_kind {
        SdfMixedSystemVerilogOwnerKind::Interface
    };
    std::string owner_identity;
    std::string endpoint_identity;
    runtime::simir::SignalId signal { };
    std::size_t width { };
    frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
    SdfMixedSystemVerilogRegion event_region {
        SdfMixedSystemVerilogRegion::Active
    };
    frontend::SourceSpan source;
};

struct SdfMixedSystemVerilogEndpoint {
    std::string boundary_path;
    SdfMixedSystemVerilogOwnerKind owner_kind {
        SdfMixedSystemVerilogOwnerKind::Interface
    };
    std::string owner_identity;
    std::string endpoint_identity;
    runtime::simir::SignalId signal { };
    std::size_t width { };
    frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
    SdfMixedSystemVerilogRegion event_region {
        SdfMixedSystemVerilogRegion::Active
    };
    SdfMixedSystemVerilogRegion sampling_region {
        SdfMixedSystemVerilogRegion::Active
    };
    SdfMixedSystemVerilogRegion evaluation_region {
        SdfMixedSystemVerilogRegion::Active
    };
    SdfMixedSystemVerilogRegion action_region {
        SdfMixedSystemVerilogRegion::Active
    };
    frontend::SourceSpan source;
    std::string canonical_identity;

    friend bool operator==(const SdfMixedSystemVerilogEndpoint&,
        const SdfMixedSystemVerilogEndpoint&) = default;
};

class SdfMixedSystemVerilogApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    SdfMixedSystemVerilogApplication(
        std::shared_ptr<const SdfMixedVerilogApplication> mixed,
        std::vector<SdfMixedSystemVerilogEndpoint> endpoints,
        std::string semantic_identity);

    [[nodiscard]] const std::shared_ptr<const SdfMixedVerilogApplication>&
    mixed() const noexcept;
    [[nodiscard]] std::span<const SdfMixedSystemVerilogEndpoint> endpoints()
        const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

private:
    std::shared_ptr<const SdfMixedVerilogApplication> mixed_;
    std::vector<SdfMixedSystemVerilogEndpoint> endpoints_;
    std::string semantic_identity_;
};

struct SdfMixedSystemVerilogLimits {
    std::size_t max_bindings { 1'000'000U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfMixedSystemVerilogResult {
    std::shared_ptr<const SdfMixedSystemVerilogApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfMixedSystemVerilogResult apply_sdf_mixed_systemverilog(
    std::shared_ptr<const SdfMixedVerilogApplication> mixed,
    const semantic::sv::Hir& hir,
    std::span<const SdfMixedSystemVerilogBinding> bindings,
    SdfMixedSystemVerilogLimits limits = { });

} // namespace fsim::app
