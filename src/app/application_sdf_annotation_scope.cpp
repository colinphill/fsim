// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/sdf_annotation_scope.hpp"

#include <algorithm>
#include <ranges>
#include <unordered_map>
#include <utility>

namespace fsim::app {
namespace {

    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::SdfHeaderKind;
    using frontend::SourceSpan;

    void append_field(std::string& target, const std::string_view value)
    {
        target += std::to_string(value.size());
        target += ':';
        target += value;
    }

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message, const SourceSpan& span)
    {
        diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), span, { } });
    }

    SdfAnnotationRoot specialization_root(
        const elaboration::SpecializationInfo& specialization)
    {
        SdfAnnotationRoot root;
        root.alias = specialization.instance;
        root.selected_identity = specialization.unit;
        switch (specialization.language) {
        case frontend::Language::Vhdl2008:
            root.language = SdfScopeRootLanguage::Vhdl;
            root.case_policy = SdfHierarchyCasePolicy::AsciiInsensitive;
            break;
        case frontend::Language::Verilog2005:
            root.language = SdfScopeRootLanguage::Verilog;
            break;
        case frontend::Language::SystemVerilog2017:
            root.language = SdfScopeRootLanguage::SystemVerilog;
            break;
        }
        return root;
    }

    struct RootCatalogEntry {
        bool available { };
        std::size_t identity_count { };
        SdfAnnotationRoot root;
    };

    bool valid_selection_shape(const SdfAnnotationScopeRequest& request,
        std::vector<Diagnostic>& diagnostics, const SourceSpan& span)
    {
        switch (request.selection) {
        case SdfScopeSelection::Single:
            if (request.root_aliases.size() == 1U)
                return true;
            diagnose(diagnostics, "FSIM-SDF-SCOPE-002",
                "single-root SDF annotation scope requires exactly one root alias",
                span);
            return false;
        case SdfScopeSelection::Set:
            if (!request.root_aliases.empty())
                return true;
            diagnose(diagnostics, "FSIM-SDF-SCOPE-002",
                "root-set SDF annotation scope requires at least one root alias",
                span);
            return false;
        case SdfScopeSelection::All:
            if (request.root_aliases.empty())
                return true;
            diagnose(diagnostics, "FSIM-SDF-SCOPE-002",
                "all-roots SDF annotation scope does not accept explicit aliases",
                span);
            return false;
        }
        return false;
    }

    std::vector<std::string> selected_aliases(
        const elaboration::ElaboratedDesign& elaborated,
        const SdfAnnotationScopeRequest& request,
        const SdfAnnotationScopeLimits& limits,
        std::vector<Diagnostic>& diagnostics, const SourceSpan& span)
    {
        if (!valid_selection_shape(request, diagnostics, span))
            return { };
        std::vector<std::string> aliases;
        if (request.selection == SdfScopeSelection::All)
            aliases = elaborated.roots();
        else
            aliases = request.root_aliases;
        if (aliases.size() > limits.max_roots) {
            diagnose(diagnostics, "FSIM-SDF-SCOPE-005",
                "SDF annotation scope exceeds the configured root limit", span);
            return { };
        }
        std::ranges::sort(aliases);
        if (std::ranges::adjacent_find(aliases) != aliases.end()) {
            diagnose(diagnostics, "FSIM-SDF-SCOPE-002",
                "SDF annotation scope contains a duplicate root alias", span);
            return { };
        }
        return aliases;
    }

    std::unordered_map<std::string_view, RootCatalogEntry> root_catalog(
        const elaboration::ElaboratedDesign& elaborated,
        std::vector<Diagnostic>& diagnostics, const SourceSpan& span)
    {
        std::unordered_map<std::string_view, RootCatalogEntry> catalog;
        catalog.reserve(elaborated.roots().size());
        for (const auto& alias : elaborated.roots()) {
            auto& entry = catalog[alias];
            if (alias.empty() || entry.available) {
                diagnose(diagnostics, "FSIM-SDF-SCOPE-001",
                    "elaborated design contains an empty or duplicate root alias",
                    span);
                return { };
            }
            entry.available = true;
        }
        for (const auto& specialization : elaborated.specializations()) {
            auto& entry = catalog[specialization.instance];
            if (!entry.available)
                continue;
            ++entry.identity_count;
            if (entry.identity_count == 1U)
                entry.root = specialization_root(specialization);
        }
        for (const auto& systemc : elaborated.systemc_instances()) {
            auto& entry = catalog[systemc.instance];
            if (!entry.available)
                continue;
            ++entry.identity_count;
            if (entry.identity_count == 1U) {
                entry.root = SdfAnnotationRoot { systemc.instance, systemc.target,
                    SdfScopeRootLanguage::SystemC,
                    SdfHierarchyCasePolicy::Sensitive };
            }
        }
        return catalog;
    }

    std::vector<SdfAnnotationRoot> collect_roots(
        const elaboration::ElaboratedDesign& elaborated,
        const std::vector<std::string>& aliases,
        std::vector<Diagnostic>& diagnostics, const SourceSpan& span)
    {
        auto catalog = root_catalog(elaborated, diagnostics, span);
        if (frontend::has_errors(diagnostics))
            return { };
        std::vector<SdfAnnotationRoot> roots;
        roots.reserve(aliases.size());
        for (const auto& alias : aliases) {
            const auto& entry = catalog[alias];
            if (!entry.available) {
                diagnose(diagnostics, "FSIM-SDF-SCOPE-002",
                    "SDF annotation root '" + alias
                        + "' is not present in the elaborated design",
                    span);
            } else if (entry.identity_count == 0U) {
                diagnose(diagnostics, "FSIM-SDF-SCOPE-001",
                    "elaborated root '" + alias + "' has no semantic identity",
                    span);
            } else if (entry.identity_count != 1U) {
                diagnose(diagnostics, "FSIM-SDF-SCOPE-001",
                    "elaborated root '" + alias
                        + "' has conflicting semantic identities",
                    span);
            } else if (entry.root.selected_identity.empty()) {
                diagnose(diagnostics, "FSIM-SDF-SCOPE-001",
                    "elaborated root '" + alias
                        + "' has an empty semantic identity",
                    span);
            } else {
                roots.push_back(entry.root);
            }
        }
        return roots;
    }

    char hierarchy_divider(const frontend::SdfFile& sdf,
        std::vector<Diagnostic>& diagnostics)
    {
        const auto* header = sdf.find_header(SdfHeaderKind::Divider);
        if (header == nullptr)
            return '.';
        if (header->canonical_value.size() != 1U
            || (header->canonical_value[0] != '.'
                && header->canonical_value[0] != '/')) {
            diagnose(diagnostics, "FSIM-SDF-SCOPE-001",
                "normalized SDF hierarchy divider is incomplete", header->span);
            return '\0';
        }
        return header->canonical_value[0];
    }

    std::optional<std::string> make_identity(const frontend::SdfIr& ir,
        const std::optional<std::string>& sdf_design_name,
        const char divider, const std::string_view project_identity,
        const std::string_view design_identity,
        const std::vector<SdfAnnotationRoot>& roots,
        const std::size_t byte_limit)
    {
        std::string identity = "fsim-sdf-scope-v1";
        append_field(identity, ir.semantic_identity());
        append_field(identity, sdf_design_name.value_or(""));
        append_field(identity, std::string_view { &divider, 1U });
        append_field(identity, project_identity);
        append_field(identity, design_identity);
        for (const auto& root : roots) {
            append_field(identity, root.alias);
            append_field(identity, root.selected_identity);
            append_field(identity, std::to_string(static_cast<unsigned>(root.language)));
            append_field(identity,
                std::to_string(static_cast<unsigned>(root.case_policy)));
            if (identity.size() > byte_limit)
                return std::nullopt;
        }
        if (identity.size() > byte_limit)
            return std::nullopt;
        return identity;
    }

} // namespace

SdfAnnotationScope::SdfAnnotationScope(
    std::shared_ptr<const frontend::SdfIr> normalized_ir,
    std::string source_name, std::string source_semantic_identity,
    std::optional<std::string> sdf_design_name, const char hierarchy_divider,
    std::string project_identity, std::string design_identity,
    std::vector<SdfAnnotationRoot> roots, std::string semantic_identity)
    : normalized_ir_(std::move(normalized_ir))
    , source_name_(std::move(source_name))
    , source_semantic_identity_(std::move(source_semantic_identity))
    , sdf_design_name_(std::move(sdf_design_name))
    , hierarchy_divider_(hierarchy_divider)
    , project_identity_(std::move(project_identity))
    , design_identity_(std::move(design_identity))
    , roots_(std::move(roots))
    , semantic_identity_(std::move(semantic_identity))
{
}

const std::shared_ptr<const frontend::SdfIr>&
SdfAnnotationScope::normalized_ir() const noexcept
{
    return normalized_ir_;
}

std::string_view SdfAnnotationScope::source_name() const noexcept
{
    return source_name_;
}

std::string_view SdfAnnotationScope::source_semantic_identity() const noexcept
{
    return source_semantic_identity_;
}

const std::optional<std::string>&
SdfAnnotationScope::sdf_design_name() const noexcept
{
    return sdf_design_name_;
}

char SdfAnnotationScope::hierarchy_divider() const noexcept
{
    return hierarchy_divider_;
}

std::string_view SdfAnnotationScope::project_identity() const noexcept
{
    return project_identity_;
}

std::string_view SdfAnnotationScope::design_identity() const noexcept
{
    return design_identity_;
}

std::span<const SdfAnnotationRoot> SdfAnnotationScope::roots() const noexcept
{
    return roots_;
}

std::string_view SdfAnnotationScope::semantic_identity() const noexcept
{
    return semantic_identity_;
}

bool SdfAnnotationScopeResult::ok() const noexcept
{
    return scope != nullptr && !frontend::has_errors(diagnostics);
}

SdfAnnotationScopeResult bind_sdf_annotation_scope(
    const frontend::SdfFile& sdf,
    const elaboration::ElaboratedDesign& elaborated,
    const std::string_view project_identity,
    const std::string_view design_identity,
    const SdfAnnotationScopeRequest& request,
    const SdfAnnotationScopeLimits limits)
{
    SdfAnnotationScopeResult result;
    if (!sdf.normalized_ir || sdf.normalized_ir->semantic_identity().empty()
        || sdf.normalized_ir->source_name() != sdf.source_name) {
        diagnose(result.diagnostics, "FSIM-SDF-SCOPE-001",
            "SDF annotation scope requires a complete normalized IR", sdf.span);
        return result;
    }
    if (project_identity.empty() || design_identity.empty()
        || request.expected_project_identity.empty()
        || request.expected_design_identity.empty()
        || elaborated.roots().empty()) {
        diagnose(result.diagnostics, "FSIM-SDF-SCOPE-001",
            "SDF annotation scope identity is incomplete", sdf.span);
        return result;
    }
    if (elaborated.roots().size() > limits.max_roots) {
        diagnose(result.diagnostics, "FSIM-SDF-SCOPE-005",
            "elaborated design exceeds the configured annotation root limit",
            sdf.span);
        return result;
    }
    if (request.expected_project_identity != project_identity) {
        diagnose(result.diagnostics, "FSIM-SDF-SCOPE-003",
            "SDF annotation scope belongs to project '"
                + request.expected_project_identity + "', not current project '"
                + std::string { project_identity } + "'",
            sdf.span);
        return result;
    }
    if (request.expected_design_identity != design_identity) {
        diagnose(result.diagnostics, "FSIM-SDF-SCOPE-004",
            "SDF annotation scope targets stale design identity '"
                + request.expected_design_identity + "', not current design '"
                + std::string { design_identity } + "'",
            sdf.span);
        return result;
    }

    const auto aliases = selected_aliases(
        elaborated, request, limits, result.diagnostics, sdf.span);
    if (frontend::has_errors(result.diagnostics))
        return result;
    auto roots = collect_roots(elaborated, aliases, result.diagnostics, sdf.span);
    const auto divider = hierarchy_divider(sdf, result.diagnostics);
    if (frontend::has_errors(result.diagnostics)
        || roots.size() != aliases.size())
        return result;

    std::optional<std::string> sdf_design_name;
    if (const auto* header = sdf.find_header(SdfHeaderKind::Design))
        sdf_design_name = header->canonical_value;
    const auto identity = make_identity(*sdf.normalized_ir, sdf_design_name,
        divider, project_identity, design_identity, roots,
        limits.max_identity_bytes);
    if (!identity) {
        diagnose(result.diagnostics, "FSIM-SDF-SCOPE-005",
            "SDF annotation scope identity exceeds the configured byte limit",
            sdf.span);
        return result;
    }
    result.scope = std::make_shared<const SdfAnnotationScope>(sdf.normalized_ir,
        sdf.source_name, std::string { sdf.normalized_ir->semantic_identity() },
        std::move(sdf_design_name), divider, std::string { project_identity },
        std::string { design_identity }, std::move(roots), *identity);
    return result;
}

} // namespace fsim::app
