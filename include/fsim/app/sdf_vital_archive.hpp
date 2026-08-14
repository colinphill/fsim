// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_effective_archive.hpp"
#include "fsim/app/sdf_foreign_interfaces.hpp"

#include <memory>
#include <string>
#include <vector>

namespace fsim::app {

class SdfVitalArchiveApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    SdfVitalArchiveApplication(
        std::shared_ptr<const SdfVitalReannotationApplication> vital,
        std::shared_ptr<const SdfForeignInterfaceApplication> foreign,
        SdfEffectiveArchiveSnapshot snapshot,
        std::string semantic_identity);

    [[nodiscard]] const std::shared_ptr<const SdfVitalReannotationApplication>&
    vital() const noexcept;
    [[nodiscard]] const std::shared_ptr<const SdfForeignInterfaceApplication>&
    foreign() const noexcept;
    [[nodiscard]] const SdfEffectiveArchiveSnapshot& snapshot() const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

private:
    std::shared_ptr<const SdfVitalReannotationApplication> vital_;
    std::shared_ptr<const SdfForeignInterfaceApplication> foreign_;
    SdfEffectiveArchiveSnapshot snapshot_;
    std::string semantic_identity_;
};

struct SdfVitalArchiveResult {
    std::shared_ptr<const SdfVitalArchiveApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfVitalArchiveResult build_sdf_vital_archive(
    std::shared_ptr<const SdfVitalReannotationApplication> vital,
    std::shared_ptr<const SdfForeignInterfaceApplication> foreign,
    SdfEffectiveArchiveLimits limits = { });

[[nodiscard]] SdfEffectiveArchiveEncodeResult encode_sdf_vital_archive(
    const SdfVitalArchiveApplication& application,
    SdfEffectiveArchiveKind kind,
    std::string_view producer_identity,
    SdfEffectiveArchiveLimits limits = { });

[[nodiscard]] SdfEffectiveArchiveDecodeResult decode_sdf_vital_archive(
    std::span<const std::byte> archive,
    SdfEffectiveArchiveKind expected_kind,
    std::string_view expected_producer_identity,
    std::string_view expected_policy_identity,
    SdfEffectiveArchiveLimits limits = { });

} // namespace fsim::app
