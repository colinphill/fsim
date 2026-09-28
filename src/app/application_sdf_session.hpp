// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/application.hpp"
#include "fsim/app/sdf_control.hpp"
#include "fsim/app/sdf_mapping_validation.hpp"
#include "fsim/artifact/design.hpp"
#include "fsim/library/artifact.hpp"

#include <string>
#include <vector>

namespace fsim::app {

class SdfSessionPublication final {
public:
    frontend::SdfFile file;
    std::shared_ptr<const SdfAnnotationSummary> summary;
    std::string source_text;
    SdfDelaySelection selection { SdfDelaySelection::Typical };
};

struct SdfSessionPlan {
    elaboration::ElaboratedDesign design;
    std::vector<std::shared_ptr<const SdfSessionPublication>> publications;
    std::shared_ptr<const SdfControlApplication> control;
};

[[nodiscard]] SdfControlRequest make_cli_sdf_request(
    const cli::Invocation& invocation, SdfControlPhase phase,
    project::DelayMode default_mode);

[[nodiscard]] std::optional<SdfSessionPlan> plan_sdf_session_inputs(
    const elaboration::ElaboratedDesign& design,
    std::string_view time_resolution, std::string_view design_identity,
    SdfControlRequest request,
    std::span<const std::filesystem::path> resolved_paths,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool apply_sdf_session_inputs(BuiltProject& built,
    const SdfControlRequest& request, diagnostic::Engine& diagnostics);

void apply_sdf_session_cache_identity(BuiltProject& built,
    const SdfControlApplication& control);

void apply_sdf_session_cache_identity(std::string& cache_key,
    std::vector<std::string>& specialization_cache_keys,
    const SdfControlApplication& control);

[[nodiscard]] bool append_sdf_session_payloads(const BuiltProject& built,
    artifact::DesignMetadata& metadata,
    std::vector<library::PortablePayload>& payloads,
    diagnostic::Engine& diagnostics);

} // namespace fsim::app
