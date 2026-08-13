// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/application.hpp"
#include "fsim/app/sdf_portable_archive.hpp"

namespace fsim::app {

class SdfPhaseArtifact final {
public:
    SdfPhaseArtifact(std::shared_ptr<const SdfPortableArchiveSnapshot> snapshot,
        std::vector<std::byte> bytes, bool cache_identity_applied = false);

    [[nodiscard]] const SdfPortableArchiveSnapshot& snapshot() const noexcept;
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
    [[nodiscard]] bool cache_identity_applied() const noexcept;

private:
    std::shared_ptr<const SdfPortableArchiveSnapshot> snapshot_;
    std::vector<std::byte> bytes_;
    bool cache_identity_applied_ { };
};

[[nodiscard]] bool install_sdf_phase_artifact(BuiltProject& project,
    std::span<const std::byte> bytes,
    const artifact::DesignSdfAnnotation& expected_annotation,
    diagnostic::Engine& diagnostics,
    SdfPortableArchiveLimits limits = { });

[[nodiscard]] std::span<const std::shared_ptr<const SdfPhaseArtifact>>
sdf_phase_artifacts(const BuiltProject& project) noexcept;

[[nodiscard]] bool append_sdf_phase_payloads(const BuiltProject& project,
    artifact::DesignMetadata& metadata,
    std::vector<library::PortablePayload>& payloads,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool restore_sdf_phase_artifacts(
    const std::filesystem::path& directory,
    const artifact::DesignMetadata& metadata, BuiltProject& project,
    diagnostic::Engine& diagnostics,
    SdfPortableArchiveLimits limits = { });

} // namespace fsim::app
