// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_scheduling.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfDriveTimingBindingRole : std::uint8_t {
    path_driver,
    propagated_driver,
    switch_unidirectional,
    switch_bidirectional
};

struct SdfDriveTimingBinding {
    std::string path_identity;
    runtime::simir::ProcessId process { };
    runtime::simir::SignalId signal { };
    std::uint64_t offset { };
    std::uint64_t width { };
    runtime::simir::ResolutionKind resolution {
        runtime::simir::ResolutionKind::none
    };
    runtime::simir::DriveStrength strength;
    SdfDriveTimingBindingRole role { SdfDriveTimingBindingRole::path_driver };
    std::string canonical_identity;

    friend bool operator==(const SdfDriveTimingBinding&,
        const SdfDriveTimingBinding&) = default;
};

class SdfDriveTimingApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    [[nodiscard]] const std::shared_ptr<const SdfSchedulingApplication>&
    scheduling() const noexcept;
    [[nodiscard]] std::span<const SdfDriveTimingBinding> bindings() const
        noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

    SdfDriveTimingApplication(
        std::shared_ptr<const SdfSchedulingApplication> scheduling,
        std::vector<SdfDriveTimingBinding> bindings,
        std::string semantic_identity);

private:
    std::shared_ptr<const SdfSchedulingApplication> scheduling_;
    std::vector<SdfDriveTimingBinding> bindings_;
    std::string semantic_identity_;
};

struct SdfDriveTimingLimits {
    std::size_t max_bindings { 4'000'000U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfDriveTimingResult {
    std::shared_ptr<const SdfDriveTimingApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfDriveTimingResult apply_sdf_drive_timing(
    std::shared_ptr<const SdfSchedulingApplication> scheduling,
    SdfDriveTimingLimits limits = { });

} // namespace fsim::app
