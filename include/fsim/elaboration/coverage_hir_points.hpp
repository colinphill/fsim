// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_external_exclusions.hpp"
#include "fsim/elaboration/coverage_fsm_hints.hpp"
#include "fsim/elaboration/coverage_inventory.hpp"
#include "fsim/frontend/coverage_source_control.hpp"
#include "fsim/semantic/compiled_design.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fsim::elaboration {

struct SpecializationInfo;

struct CoverageHirSource {
    CoverageInventorySource inventory;
    std::vector<frontend::CoverageSourceExclusion> exclusions;
};

// The SystemVerilog design elements whose instances have code coverage:
// modules, interfaces and programs (not packages or compilation units).
[[nodiscard]] constexpr bool covered_systemverilog_unit(
    const semantic::sv::UnitKind kind) noexcept
{
    return kind == semantic::sv::UnitKind::module
        || kind == semantic::sv::UnitKind::interface
        || kind == semantic::sv::UnitKind::program;
}

struct CoverageHirContext {
    std::span<const CoverageHirSource> sources;
    // The project manifest's FSM hints, each for one instance.
    std::span<const project::CoverageFsmHintEntry> fsm_hints;
};

// The FSM coverage hints for one instance of a unit: its SystemVerilog
// module attribute instances or VHDL fsm_* attribute specifications, and the
// manifest's hints for that instance.
[[nodiscard]] CoverageFsmHintResult coverage_hir_fsm_hints(
    const semantic::CompiledDesign& compiled, semantic::UnitId unit,
    std::span<const project::CoverageFsmHintEntry> manifest,
    std::string_view instance) noexcept;

enum class CoverageHirPointError : std::uint8_t {
    None,
    ResourceLimit,
    InvalidProcess,
    InvalidStatement,
    InvalidSource,
    InvalidSpan,
    MissingBranchArm,
    DuplicatePoint,
};

struct CoverageHirPointResult {
    CoverageInstanceInventoryDraft draft;
    CoverageHirPointError error { CoverageHirPointError::None };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CoverageHirPointError::None;
    }
};

enum class CoverageConditionOperator : std::uint8_t {
    atom,
    logical_not,
    logical_and,
    logical_or,
    logical_nand,
    logical_nor,
    logical_xor,
    logical_xnor,
};

struct CoverageConditionNode {
    CoverageConditionOperator op { CoverageConditionOperator::atom };
    semantic::ExpressionId expression;
    // Operand node indices; `right` is unused by logical_not.
    std::uint32_t left { };
    std::uint32_t right { };
    // The atom's position in CoverageConditionTree::atoms.
    std::uint32_t atom { };
};

/// The logical structure of one decision: its atoms are the operands of
/// the logical operators (SV `!`, `&&`, `||`; VHDL `not`, `and`, `or`,
/// `nand`, `nor`, `xor`, `xnor` without user overloads), in source order.
struct CoverageConditionTree {
    std::vector<CoverageConditionNode> nodes;
    std::uint32_t root { };
    std::vector<semantic::ExpressionId> atoms;
};

struct CoverageConditionStep {
    std::uint32_t atom { };
    bool value { };
};

/// One reachable evaluation of a decision: the atoms it evaluates, in
/// evaluation order, and the decision's result. And/or forms skip their
/// right operand when the left one decides the result.
struct CoverageExpressionRow {
    std::vector<CoverageConditionStep> steps;
    bool result { };
};

using CoverageExpressionLookup = std::function<
    std::optional<semantic::CompiledExpressionView>(semantic::ExpressionId)>;
using CoverageDeclarationLookup = std::function<
    std::optional<semantic::CompiledDeclarationView>(semantic::DeclarationId)>;

inline constexpr std::size_t kCoverageConditionMaximumAtoms = 16U;
// The widest object toggle coverage counts; a wider one is not covered.
inline constexpr std::size_t kCoverageToggleMaximumWidth = 4096U;
inline constexpr std::size_t kCoverageExpressionMaximumRows = 64U;

/// The condition tree of a decision with at least one logical operator.
/// None when the decision has no logical operator, too many atoms, or a
/// call or update that re-evaluating its atoms could repeat.
[[nodiscard]] std::optional<CoverageConditionTree> coverage_condition_tree(
    semantic::ExpressionId decision,
    const CoverageExpressionLookup& expressions,
    const CoverageDeclarationLookup& declarations);

/// An SV conditional operator (`?:`) in a continuous assignment's value
/// whose outcomes have branch points: its condition can be evaluated again.
struct CoverageTernary {
    semantic::ExpressionId condition;
    semantic::ExpressionId when_true;
    semantic::ExpressionId when_false;
};

/// The conditional operator `id` names, if its outcomes are covered.
[[nodiscard]] std::optional<CoverageTernary> coverage_ternary(
    semantic::ExpressionId id, const CoverageExpressionLookup& expressions,
    const CoverageDeclarationLookup& declarations);

/// The reachable evaluation rows of `tree`, or none beyond
/// kCoverageExpressionMaximumRows.
[[nodiscard]] std::vector<CoverageExpressionRow> coverage_expression_rows(
    const CoverageConditionTree& tree);

// Discover source-backed executable statements in selected SystemVerilog
// module processes. The inventory includes both conditional outcomes even
// when specialization later removes one path. Callable and synthetic bodies
// have separate ownership and are outside this module-process scope.
[[nodiscard]] CoverageHirPointResult discover_hir_module_coverage_points(
    const semantic::CompiledDesign& compiled,
    const SpecializationInfo& specialization,
    std::span<const CoverageHirSource> sources,
    std::size_t maximum_statements = 1U << 20U,
    const CoverageExternalExclusionPlan* external_exclusions = nullptr)
    noexcept;

} // namespace fsim::elaboration
