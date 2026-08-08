// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/application.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/semantic/design_ir.hpp"
#include "fsim/semantic/model.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace fsim::app {

inline constexpr std::uint32_t kRuntimeStateSchema = 18;
inline constexpr std::uint32_t kSemanticStateSchema = 2;
inline constexpr std::uint32_t kDesignIrStateSchema = 2;
inline constexpr std::uint32_t kClassStateSchema = 9;
inline constexpr std::uint32_t kSystemVerilogConstraintHirStateSchema = 3;
inline constexpr std::uint32_t kSystemVerilogCoverageStateSchema = 1;
inline constexpr std::uint32_t kSystemVerilogUvmStateSchema = 1;

[[nodiscard]] std::optional<std::string> serialize_runtime_state(
    const elaboration::ElaboratedDesign& design,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<elaboration::ElaboratedDesign>
deserialize_runtime_state(
    std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::string> serialize_semantic_state(
    const semantic::Model& model,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<semantic::Model> deserialize_semantic_state(
    std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::string> serialize_design_ir_state(
    const semantic::design::DesignIr& design,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<semantic::design::DesignIr>
deserialize_design_ir_state(
    std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::string> serialize_class_state(
    std::span<const frontend::SystemVerilogClassSpecialization> classes,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<std::vector<
    frontend::SystemVerilogClassSpecialization>>
deserialize_class_state(
    std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::string>
serialize_systemverilog_constraint_hir_state(
    const semantic::sv::Hir& hir,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<semantic::sv::Hir>
deserialize_systemverilog_constraint_hir_state(
    std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::string>
serialize_systemverilog_coverage_state(
    const frontend::SystemVerilogCoverageState& state,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<frontend::SystemVerilogCoverageState>
deserialize_systemverilog_coverage_state(
    std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::string>
serialize_systemverilog_uvm_state(
    const runtime::SystemVerilogUvmCheckpointArtifact& state,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<runtime::SystemVerilogUvmCheckpointArtifact>
deserialize_systemverilog_uvm_state(
    std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool publish_design_artifact(
    const project::Config& config,
    const BuiltProject& project,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<BuiltProject> load_design_artifact(
    const std::filesystem::path& directory,
    diagnostic::Engine& diagnostics);

}  // namespace fsim::app
