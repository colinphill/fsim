// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/sdf_cell_resolution.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <utility>

namespace fsim::app {
namespace {

    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::SdfInstanceSelectorKind;
    using frontend::SdfIrCell;
    using frontend::SourceSpan;

    struct Candidate {
        SdfResolvedInstance resolved;
        std::vector<std::string> segments;
        std::vector<SdfHierarchyCasePolicy> segment_policies;
    };

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message, const SourceSpan& span)
    {
        diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), span, { } });
    }

    void append_field(std::string& target, const std::string_view value)
    {
        target += std::to_string(value.size());
        target += ':';
        target += value;
    }

    bool segment_matches(const std::string_view left, const std::string_view right,
        const SdfHierarchyCasePolicy policy)
    {
        if (left.size() != right.size())
            return false;
        if (policy == SdfHierarchyCasePolicy::Sensitive)
            return left == right;
        return std::ranges::equal(left, right, [](const char lhs, const char rhs) {
            return std::tolower(static_cast<unsigned char>(lhs))
                == std::tolower(static_cast<unsigned char>(rhs));
        });
    }

    std::optional<std::string> configuration_entity(
        const elaboration::SpecializationInfo& specialization)
    {
        const auto entity_value = [](const std::string& identity)
            -> std::optional<std::string> {
            constexpr std::string_view marker = ";entity=";
            const auto begin = identity.find(marker);
            if (begin == std::string::npos)
                return std::nullopt;
            const auto value_begin = begin + marker.size();
            const auto end = identity.find(';', value_begin);
            return identity.substr(value_begin, end - value_begin);
        };
        for (const auto& [name, identity] :
            specialization.parameter_identity_values) {
            if (name != "__configuration")
                continue;
            if (auto entity = entity_value(identity))
                return entity;
        }
        return std::nullopt;
    }

    std::string identity_cell_type(const std::string_view identity)
    {
        auto body = identity;
        if (const auto colon = body.find(':'); colon != std::string_view::npos)
            body.remove_prefix(colon + 1U);
        if (const auto dot = body.find('.'); dot != std::string_view::npos)
            body.remove_prefix(dot + 1U);
        if (const auto architecture = body.find('(');
            architecture != std::string_view::npos) {
            body = body.substr(0U, architecture);
        }
        return std::string { body };
    }

    SdfScopeRootLanguage scope_language(const frontend::Language language)
    {
        switch (language) {
        case frontend::Language::Vhdl2008:
            return SdfScopeRootLanguage::Vhdl;
        case frontend::Language::Verilog2005:
            return SdfScopeRootLanguage::Verilog;
        case frontend::Language::SystemVerilog2017:
            return SdfScopeRootLanguage::SystemVerilog;
        }
        return SdfScopeRootLanguage::SystemVerilog;
    }

    SdfHierarchyCasePolicy language_case(const SdfScopeRootLanguage language)
    {
        return language == SdfScopeRootLanguage::Vhdl
            ? SdfHierarchyCasePolicy::AsciiInsensitive
            : SdfHierarchyCasePolicy::Sensitive;
    }

    std::string_view root_alias(const std::string_view path)
    {
        const auto divider = path.find('.');
        return path.substr(0U, divider);
    }

    std::unordered_map<std::string_view, const SdfAnnotationRoot*> scope_roots(
        const SdfAnnotationScope& scope)
    {
        std::unordered_map<std::string_view, const SdfAnnotationRoot*> roots;
        roots.reserve(scope.roots().size());
        for (const auto& root : scope.roots())
            roots.emplace(root.alias, &root);
        return roots;
    }

    std::vector<Candidate> raw_candidates(const SdfAnnotationScope& scope,
        const elaboration::ElaboratedDesign& elaborated,
        const SdfCellResolutionLimits& limits,
        std::vector<Diagnostic>& diagnostics)
    {
        std::vector<Candidate> candidates;
        const auto total = elaborated.specializations().size()
            + elaborated.systemc_instances().size();
        if (total > limits.max_candidates) {
            diagnose(diagnostics, "FSIM-SDF-RESOLVE-005",
                "elaborated SDF candidate set exceeds the configured limit",
                scope.normalized_ir()->cells().empty()
                    ? SourceSpan { }
                    : scope.normalized_ir()->cells().front().span);
            return candidates;
        }
        candidates.reserve(total);
        auto roots = scope_roots(scope);
        for (const auto& specialization : elaborated.specializations()) {
            const auto* root = roots[root_alias(specialization.instance)];
            if (root == nullptr)
                continue;
            const auto language = scope_language(specialization.language);
            auto cell_type = configuration_entity(specialization)
                                 .value_or(identity_cell_type(specialization.unit));
            candidates.push_back(Candidate { SdfResolvedInstance {
                                                 SdfResolvedInstanceKind::Hdl, specialization.id, root->alias,
                                                 specialization.instance, specialization.unit, std::move(cell_type),
                                                 language, language_case(language), specialization.is_cell },
                { }, { } });
        }
        for (const auto& systemc : elaborated.systemc_instances()) {
            const auto* root = roots[root_alias(systemc.instance)];
            if (root == nullptr)
                continue;
            candidates.push_back(Candidate { SdfResolvedInstance {
                                                 SdfResolvedInstanceKind::SystemC, systemc.id, root->alias,
                                                 systemc.instance, systemc.target,
                                                 identity_cell_type(systemc.target), SdfScopeRootLanguage::SystemC,
                                                 SdfHierarchyCasePolicy::Sensitive, false },
                { }, { } });
        }
        return candidates;
    }

    bool candidate_order(const Candidate& left, const Candidate& right)
    {
        if (left.resolved.instance_path.size()
            != right.resolved.instance_path.size()) {
            return left.resolved.instance_path.size()
                < right.resolved.instance_path.size();
        }
        if (left.resolved.instance_path != right.resolved.instance_path)
            return left.resolved.instance_path < right.resolved.instance_path;
        if (left.resolved.kind != right.resolved.kind)
            return left.resolved.kind < right.resolved.kind;
        return left.resolved.declaration_id < right.resolved.declaration_id;
    }

    std::optional<std::size_t> parent_candidate(const Candidate& candidate,
        std::unordered_map<std::string_view, std::size_t>& path_indices)
    {
        const auto path = std::string_view { candidate.resolved.instance_path };
        for (std::size_t divider = path.size(); divider > 0U; --divider) {
            const auto character = divider - 1U;
            if (path[character] != '.')
                continue;
            const auto index = path_indices[path.substr(0U, character)];
            if (index != 0U)
                return index - 1U;
        }
        return std::nullopt;
    }

    bool build_segments(std::vector<Candidate>& candidates,
        const SdfAnnotationScope& scope, std::vector<Diagnostic>& diagnostics)
    {
        std::ranges::sort(candidates, candidate_order);
        std::unordered_map<std::string_view, std::size_t> path_indices;
        path_indices.reserve(candidates.size());
        for (std::size_t index = 0U; index < candidates.size(); ++index) {
            if (!path_indices
                    .try_emplace(candidates[index].resolved.instance_path, index + 1U)
                    .second) {
                diagnose(diagnostics, "FSIM-SDF-RESOLVE-001",
                    "elaborated instance path '"
                        + candidates[index].resolved.instance_path
                        + "' has duplicate semantic owners",
                    scope.normalized_ir()->cells().empty()
                        ? SourceSpan { }
                        : scope.normalized_ir()->cells().front().span);
                return false;
            }
        }
        auto roots = scope_roots(scope);
        std::unordered_map<std::string_view, std::size_t> resolved_roots;
        resolved_roots.reserve(scope.roots().size());
        for (std::size_t index = 0U; index < candidates.size(); ++index) {
            auto& candidate = candidates[index];
            const auto* root = roots[root_alias(candidate.resolved.instance_path)];
            if (root == nullptr)
                continue;
            if (candidate.resolved.instance_path == root->alias) {
                ++resolved_roots[root->alias];
                candidate.segments = { root->alias };
                candidate.segment_policies = { root->case_policy };
                if (candidate.resolved.unit_identity != root->selected_identity
                    || candidate.resolved.language != root->language
                    || candidate.resolved.case_policy != root->case_policy) {
                    diagnose(diagnostics, "FSIM-SDF-RESOLVE-001",
                        "SDF annotation scope root '" + root->alias
                            + "' is stale for semantic unit '"
                            + candidate.resolved.unit_identity + "'",
                        scope.normalized_ir()->cells().empty()
                            ? SourceSpan { }
                            : scope.normalized_ir()->cells().front().span);
                    return false;
                }
                continue;
            }
            const auto parent = parent_candidate(candidate, path_indices);
            if (!parent) {
                diagnose(diagnostics, "FSIM-SDF-RESOLVE-001",
                    "elaborated instance '" + candidate.resolved.instance_path
                        + "' has no semantic parent inside annotation scope",
                    scope.normalized_ir()->cells().empty()
                        ? SourceSpan { }
                        : scope.normalized_ir()->cells().front().span);
                return false;
            }
            const auto& parent_candidate_value = candidates[*parent];
            candidate.segments = parent_candidate_value.segments;
            candidate.segment_policies = parent_candidate_value.segment_policies;
            candidate.segments.push_back(candidate.resolved.instance_path.substr(
                parent_candidate_value.resolved.instance_path.size() + 1U));
            candidate.segment_policies.push_back(
                parent_candidate_value.resolved.case_policy);
        }
        for (const auto& root : scope.roots()) {
            if (resolved_roots[root.alias] != 1U) {
                diagnose(diagnostics, "FSIM-SDF-RESOLVE-001",
                    "SDF annotation scope root '" + root.alias
                        + "' is missing or duplicated in the elaborated design",
                    scope.normalized_ir()->cells().empty()
                        ? SourceSpan { }
                        : scope.normalized_ir()->cells().front().span);
                return false;
            }
        }
        std::ranges::sort(candidates, [](const Candidate& left, const Candidate& right) {
            if (left.resolved.instance_path != right.resolved.instance_path)
                return left.resolved.instance_path < right.resolved.instance_path;
            if (left.resolved.kind != right.resolved.kind)
                return left.resolved.kind < right.resolved.kind;
            return left.resolved.declaration_id < right.resolved.declaration_id;
        });
        return true;
    }

    bool path_matches(const Candidate& candidate,
        const std::span<const std::string> sdf_segments, const bool relative)
    {
        const auto offset = relative ? 1U : 0U;
        if (candidate.segments.size() < offset
            || candidate.segments.size() - offset != sdf_segments.size()) {
            return false;
        }
        for (std::size_t index = 0U; index < sdf_segments.size(); ++index) {
            if (!segment_matches(candidate.segments[index + offset], sdf_segments[index],
                    candidate.segment_policies[index + offset])) {
                return false;
            }
        }
        return true;
    }

    bool matches_selector(const Candidate& candidate, const SdfIrCell& cell)
    {
        switch (cell.instance_kind) {
        case SdfInstanceSelectorKind::Empty:
            return candidate.segments.size() == 1U;
        case SdfInstanceSelectorKind::Wildcard:
            return !cell.wildcard_requires_physical_primitive
                || candidate.resolved.physical_primitive;
        case SdfInstanceSelectorKind::Exact:
            if (!cell.instance)
                return false;
            return path_matches(candidate, cell.instance->segments, false)
                || path_matches(candidate, cell.instance->segments, true);
        }
        return false;
    }

    std::string candidate_summary(const std::vector<Candidate>& candidates,
        const SdfCellResolutionLimits& limits)
    {
        std::string summary;
        const auto count = std::min(candidates.size(), limits.max_reported_candidates);
        for (std::size_t index = 0U; index < count; ++index) {
            summary += index == 0U ? "; candidates: " : ", ";
            summary += candidates[index].resolved.instance_path;
            summary += '[';
            summary += candidates[index].resolved.cell_type;
            summary += ']';
        }
        if (count < candidates.size())
            summary += ", ...";
        return summary;
    }

    std::vector<SdfResolvedInstance> resolve_cell(const SdfIrCell& cell,
        const std::vector<Candidate>& candidates,
        const SdfCellResolutionLimits& limits,
        std::vector<Diagnostic>& diagnostics)
    {
        std::vector<SdfResolvedInstance> matches;
        for (const auto& candidate : candidates) {
            if (segment_matches(cell.cell_type, candidate.resolved.cell_type,
                    candidate.resolved.case_policy)
                && matches_selector(candidate, cell)) {
                if (matches.size() >= limits.max_matches) {
                    diagnose(diagnostics, "FSIM-SDF-RESOLVE-005",
                        "SDF cell resolution exceeds the configured match limit",
                        cell.span);
                    return { };
                }
                matches.push_back(candidate.resolved);
            }
        }
        if (matches.empty()) {
            diagnose(diagnostics, "FSIM-SDF-RESOLVE-002",
                "SDF cell '" + cell.cell_type
                    + "' has no matching elaborated instance"
                    + candidate_summary(candidates, limits),
                cell.span);
        } else if (cell.instance_kind != SdfInstanceSelectorKind::Wildcard
            && matches.size() != 1U) {
            std::string message = "SDF cell '" + cell.cell_type
                + "' resolves ambiguously to";
            for (const auto& match : matches)
                message += " " + match.instance_path;
            diagnose(diagnostics, "FSIM-SDF-RESOLVE-003", std::move(message),
                cell.span);
            return { };
        }
        return matches;
    }

    std::optional<std::string> resolution_identity(
        const SdfAnnotationScope& scope, const std::vector<SdfResolvedCell>& cells,
        const std::size_t limit)
    {
        std::string identity = "fsim-sdf-cell-resolution-v1";
        append_field(identity, scope.semantic_identity());
        for (const auto& cell : cells) {
            append_field(identity, std::to_string(cell.cell_id));
            append_field(identity, cell.source_identity);
            for (const auto& target : cell.targets) {
                append_field(identity, std::to_string(static_cast<unsigned>(target.kind)));
                append_field(identity, std::to_string(target.declaration_id));
                append_field(identity, target.instance_path);
                append_field(identity, target.unit_identity);
            }
            if (identity.size() > limit)
                return std::nullopt;
        }
        return identity.size() > limit ? std::nullopt
                                       : std::optional<std::string> { identity };
    }

} // namespace

SdfCellResolution::SdfCellResolution(
    std::shared_ptr<const SdfAnnotationScope> scope,
    std::vector<SdfResolvedCell> cells, std::string semantic_identity)
    : scope_(std::move(scope))
    , cells_(std::move(cells))
    , semantic_identity_(std::move(semantic_identity))
{
}

const std::shared_ptr<const SdfAnnotationScope>&
SdfCellResolution::scope() const noexcept
{
    return scope_;
}

std::span<const SdfResolvedCell> SdfCellResolution::cells() const noexcept
{
    return cells_;
}

const SdfResolvedCell* SdfCellResolution::find_cell(
    const std::uint64_t cell_id) const noexcept
{
    if (cell_id == 0U || cell_id > cells_.size())
        return nullptr;
    const auto& cell = cells_[static_cast<std::size_t>(cell_id - 1U)];
    return cell.cell_id == cell_id ? &cell : nullptr;
}

std::string_view SdfCellResolution::semantic_identity() const noexcept
{
    return semantic_identity_;
}

bool SdfCellResolutionResult::ok() const noexcept
{
    return resolution != nullptr && !frontend::has_errors(diagnostics);
}

SdfCellResolutionResult resolve_sdf_cells(
    std::shared_ptr<const SdfAnnotationScope> scope,
    const elaboration::ElaboratedDesign& elaborated,
    const SdfCellResolutionLimits limits)
{
    SdfCellResolutionResult result;
    if (!scope || !scope->normalized_ir() || scope->roots().empty()) {
        diagnose(result.diagnostics, "FSIM-SDF-RESOLVE-001",
            "SDF cell resolution requires a complete annotation scope", { });
        return result;
    }
    auto candidates
        = raw_candidates(*scope, elaborated, limits, result.diagnostics);
    if (frontend::has_errors(result.diagnostics)
        || !build_segments(candidates, *scope, result.diagnostics)) {
        return result;
    }

    std::vector<SdfResolvedCell> cells;
    cells.reserve(scope->normalized_ir()->cells().size());
    for (const auto& cell : scope->normalized_ir()->cells()) {
        auto targets = resolve_cell(cell, candidates, limits, result.diagnostics);
        cells.push_back(SdfResolvedCell { cell.id, cell.source_identity,
            cell.instance_kind, std::move(targets) });
    }
    if (frontend::has_errors(result.diagnostics))
        return result;
    const auto identity
        = resolution_identity(*scope, cells, limits.max_identity_bytes);
    if (!identity) {
        diagnose(result.diagnostics, "FSIM-SDF-RESOLVE-005",
            "SDF cell-resolution identity exceeds the configured byte limit",
            scope->normalized_ir()->cells().empty()
                ? SourceSpan { }
                : scope->normalized_ir()->cells().front().span);
        return result;
    }
    result.resolution = std::make_shared<const SdfCellResolution>(
        std::move(scope), std::move(cells), *identity);
    return result;
}

} // namespace fsim::app
