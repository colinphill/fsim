// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_schema.hpp"
#include "fsim/artifact/design.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace fsim::app {

enum class SdfDelaySelectionPolicy { Minimum, Typical, Maximum };

struct SdfArtifactIdentityLimits {
    std::size_t max_selected_roots { 4096U };
    std::size_t max_semantic_units { 1U << 20U };
    std::size_t max_semantic_objects { 1U << 22U };
    std::size_t max_identity_bytes { 64U << 20U };
};

struct SdfArtifactIdentityResult {
    std::optional<artifact::DesignSdfAnnotation> annotation;
    std::vector<frontend::Diagnostic> diagnostics;
    [[nodiscard]] bool ok() const noexcept
    {
        return annotation.has_value() && diagnostics.empty();
    }
};

struct SdfArtifactApplyResult {
    std::optional<artifact::DesignMetadata> metadata;
    std::vector<frontend::Diagnostic> diagnostics;
    [[nodiscard]] bool ok() const noexcept
    {
        return metadata.has_value() && diagnostics.empty();
    }
};

[[nodiscard]] const char* to_string(SdfDelaySelectionPolicy policy) noexcept;

[[nodiscard]] std::string compute_sdf_artifact_cache_key(
    const artifact::DesignSdfAnnotation& annotation);

[[nodiscard]] std::string compute_sdf_semantic_object_identity(
    const SdfResolvedNodeEndpoints& node,
    const SdfResolvedEndpoint& endpoint);

[[nodiscard]] SdfArtifactIdentityResult build_sdf_artifact_identity(
    const SdfSchemaSnapshot& schema,
    const SdfAnnotationSummary& summary,
    std::string design_digest,
    SdfDelaySelectionPolicy selection_policy,
    SdfArtifactIdentityLimits limits = { });

[[nodiscard]] SdfArtifactApplyResult apply_sdf_artifact_identity(
    const artifact::DesignMetadata& metadata,
    artifact::DesignSdfAnnotation annotation);

} // namespace fsim::app
