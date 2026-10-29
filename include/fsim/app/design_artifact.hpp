// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/application.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/design_ir.hpp"
#include "fsim/semantic/model.hpp"

#include <cstddef>
#include <optional>
#include <istream>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

inline constexpr std::uint32_t kRuntimeStateSchema = 91;
inline constexpr std::uint32_t kSemanticStateSchema = 4;
inline constexpr std::uint32_t kDesignIrStateSchema = 5;
inline constexpr std::uint32_t kCompiledHirBundleSchema = 3;
inline constexpr std::uint32_t kSystemVerilogConstraintHirStateSchema = 14;
inline constexpr std::uint32_t kSystemVerilogCoverageStateSchema = 7;
inline constexpr std::uint32_t kSystemVerilogUvmStateSchema = 3;
inline constexpr std::uint32_t kSystemVerilogClassSpecializationStateSchema = 1;
inline constexpr std::uint32_t kVhdlHirStateSchema = 8;
// A UVM testbench's compiled HIR decodes to about 270 MiB.
inline constexpr std::size_t kCompiledHirDecodeBudgetBytes
    = 512U * 1024U * 1024U;

[[nodiscard]] std::optional<std::string> serialize_runtime_state(
    const elaboration::ElaboratedDesign& design,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<std::string> serialize_runtime_state(
    const elaboration::ElaboratedDesign& design,
    const semantic::HierarchyPathTable& paths,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<std::string> serialize_runtime_state(
    elaboration::ElaboratedDesign&& design,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<std::string> serialize_runtime_state(
    elaboration::ElaboratedDesign&& design,
    const semantic::HierarchyPathTable& paths,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<std::string> runtime_state_checksum(
    const elaboration::ElaboratedDesignState& state,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<std::string> runtime_state_checksum(
    const elaboration::ElaboratedDesignState& state,
    const semantic::HierarchyPathTable& paths,
    diagnostic::Engine& diagnostics);
[[nodiscard]] bool serialize_runtime_state(
    const elaboration::ElaboratedDesignState& state,
    std::ostream& output,
    diagnostic::Engine& diagnostics);
[[nodiscard]] bool serialize_runtime_state(
    const elaboration::ElaboratedDesignState& state,
    const semantic::HierarchyPathTable& paths,
    std::ostream& output,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<elaboration::ElaboratedDesign>
deserialize_runtime_state(
    std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<elaboration::ElaboratedDesign>
deserialize_runtime_state(
    std::string_view bytes,
    std::string source_name,
    const semantic::HierarchyPathTable& paths,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<elaboration::ElaboratedDesign>
deserialize_runtime_state(
    std::istream& input,
    std::uint64_t size,
    std::string source_name,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<elaboration::ElaboratedDesign>
deserialize_runtime_state(
    std::istream& input,
    std::uint64_t size,
    std::string source_name,
    const semantic::HierarchyPathTable& paths,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::string> serialize_semantic_state(
    const semantic::Model& model,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<semantic::Model> deserialize_semantic_state(
    std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::string> serialize_compiled_hir_bundle(
    const semantic::CompiledDesign& design,
    diagnostic::Engine& diagnostics);
/// Publish-side encode of the design this process just elaborated: the
/// structural HIR checks run only when FSIM_VERIFY_ARTIFACT=1.
[[nodiscard]] std::optional<std::string> serialize_elaborated_compiled_hir_bundle(
    const semantic::CompiledDesign& design,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<semantic::CompiledDesign>
deserialize_compiled_hir_bundle(
    std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::string> serialize_design_ir_state(
    const semantic::design::DesignIr& design,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<std::string> serialize_design_ir_state(
    const semantic::design::DesignIr& design,
    const semantic::HierarchyPathTable& paths,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<semantic::design::DesignIr>
deserialize_design_ir_state(
    std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<semantic::design::DesignIr>
deserialize_design_ir_state(
    std::string_view bytes,
    std::string source_name,
    const semantic::HierarchyPathTable& paths,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::string>
serialize_systemverilog_constraint_hir_state(
    const semantic::sv::Hir& hir,
    const semantic::Model& semantics,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<semantic::sv::Hir>
deserialize_systemverilog_constraint_hir_state(
    std::string_view bytes,
    std::string source_name,
    const semantic::Model& semantics,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::string>
serialize_systemverilog_coverage_state(
    const runtime::SystemVerilogCoverageState& state,
    const semantic::sv::Hir& hir,
    const semantic::Model& semantics,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<runtime::SystemVerilogCoverageState>
deserialize_systemverilog_coverage_state(
    std::string_view bytes,
    std::string source_name,
    const semantic::sv::Hir& hir,
    const semantic::Model& semantics,
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

/// The compiled class specializations a design was elaborated with, so a
/// simulate of the artifact need not re-run specialize_classes.
[[nodiscard]] std::optional<std::string>
serialize_systemverilog_class_specializations(
    std::span<const semantic::sv::ClassSpecialization> specializations,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<std::vector<semantic::sv::ClassSpecialization>>
deserialize_systemverilog_class_specializations(
    std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::string> serialize_vhdl_hir_state(
    const semantic::vhdl::Hir& hir,
    const semantic::Model& semantics,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<semantic::vhdl::Hir> deserialize_vhdl_hir_state(
    std::string_view bytes,
    std::string source_name,
    const semantic::Model& semantics,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool publish_design_artifact(
    const project::Config& config,
    const BuiltProject& project,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics);
[[nodiscard]] bool publish_design_artifact(
    const project::Config& config,
    BuiltProject&& project,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<BuiltProject> load_design_artifact(
    const std::filesystem::path& directory,
    diagnostic::Engine& diagnostics);

} // namespace fsim::app
