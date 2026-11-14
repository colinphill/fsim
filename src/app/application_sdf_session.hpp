// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/application.hpp"
#include "fsim/app/built_project.hpp"
#include "fsim/app/sdf_control.hpp"
#include "fsim/app/sdf_mapping_validation.hpp"
#include "fsim/artifact/design.hpp"
#include "fsim/elaboration/sdf_net_delay_split.hpp"
#include "fsim/library/artifact.hpp"
#include "fsim/app/sdf_value_policy.hpp"
#include "fsim/cli/driver.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/frontend/sdf.hpp"
#include "fsim/project/project.hpp"

#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

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
    /// The DEVICE delays of outputs that no module path ends at (IEEE
    /// 1497-2001 5.4.1): the gates that drive them take the delays. A design
    /// that still has its processes has them in `design`; a running
    /// simulation puts them on its gates (Simulation::commit_sdf_reannotation).
    std::vector<elaboration::SdfDeviceGateDelay> device_gates;
};

[[nodiscard]] SdfControlRequest make_cli_sdf_request(
    const cli::Invocation& invocation, SdfControlPhase phase,
    project::DelayMode default_mode);

/// Where the session finds the ports of the Verilog and SystemVerilog
/// instances: NETDELAY on a net delays every input and inout port the net
/// reaches (IEEE 1364-2005 16.2.4), and the ports and their directions are in
/// the design IR and the SystemVerilog HIR, not in the elaborated design. A
/// session without them finds no load for a NETDELAY on a net.
struct SdfPortSource {
    const semantic::design::DesignIr* design_ir { };
    const semantic::sv::Hir* systemverilog_hir { };
};

/// `vital_applied` says the design was built with these SDF files, so the
/// VHDL cells they name already have their VITAL timing generics (the build
/// annotates them, IEEE 1076.4-2000 5.1.1); without it a file that names VHDL
/// cells is rejected, because the generics are elaboration constants.
[[nodiscard]] std::optional<SdfSessionPlan> plan_sdf_session_inputs(
    const elaboration::ElaboratedDesign& design,
    std::string_view time_resolution, std::string_view design_identity,
    SdfControlRequest request,
    std::span<const std::filesystem::path> resolved_paths,
    diagnostic::Engine& diagnostics, SdfPortSource ports = { },
    bool vital_applied = false);

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
