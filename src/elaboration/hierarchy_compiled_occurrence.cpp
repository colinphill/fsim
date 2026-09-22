// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_compiled_occurrence.hpp"

#include <algorithm>
#include <ranges>

namespace fsim::elaboration::elaboration_detail {
namespace {

struct HirInstanceIdentity {
    semantic::InstanceId instance;
    semantic::ScopeId scope;
    semantic::SourceSpanId source;
    semantic::OriginId origin;
    std::string_view name;
    std::string_view target;
    semantic::CompiledReferenceKind reference_kind;
    bool linkable { true };
};

std::optional<HirInstanceIdentity> hir_instance_identity(
    const semantic::CompiledInstanceView instance) noexcept
{
    if (!instance) {
        return std::nullopt;
    }
    if (instance.systemverilog != nullptr) {
        const auto& record = *instance.systemverilog;
        return HirInstanceIdentity {
            record.id,
            record.scope,
            record.source,
            record.origin,
            record.name,
            record.target.spelling,
            semantic::CompiledReferenceKind::module,
            !record.udp,
        };
    }
    const auto& record = *instance.vhdl;
    return HirInstanceIdentity {
        record.id,
        record.scope,
        record.source,
        record.origin,
        record.name,
        record.target.spelling,
        record.configuration
            ? semantic::CompiledReferenceKind::configuration
            : semantic::CompiledReferenceKind::entity,
        true,
    };
}

std::string_view unqualified_target(std::string_view target) noexcept
{
    if (const auto open = target.find('(');
        open != std::string_view::npos) {
        target = target.substr(0, open);
    }
    const auto dot = target.rfind('.');
    const auto scope = target.rfind("::");
    const auto separator = scope == std::string_view::npos
        ? dot
        : dot == std::string_view::npos
            ? scope + 1U
            : std::max(dot, scope + 1U);
    return separator == std::string_view::npos
        ? target
        : target.substr(separator + 1U);
}

bool ascii_name_equal(
    const std::string_view left,
    const std::string_view right) noexcept
{
    return left.size() == right.size()
        && std::ranges::equal(left, right, [](const char lhs, const char rhs) {
            const auto lower = [](const char value) {
                return value >= 'A' && value <= 'Z'
                    ? static_cast<char>(value - 'A' + 'a')
                    : value;
            };
            return lower(lhs) == lower(rhs);
        });
}

const semantic::CompiledReference* find_instance_reference(
    const semantic::CompiledDesign& design,
    const HirInstanceIdentity& instance,
    const semantic::UnitId owner) noexcept
{
    const semantic::CompiledReference* source_match = nullptr;
    const semantic::CompiledReference* named_match = nullptr;
    bool ambiguous_source = false;
    bool ambiguous_name = false;
    const auto target = unqualified_target(instance.target);
    for (const auto& reference : design.references()) {
        if (reference.owner != owner
            || reference.kind != instance.reference_kind
            || reference.source != instance.source) {
            continue;
        }
        if (source_match != nullptr
            && source_match->target != reference.target) {
            ambiguous_source = true;
        } else if (source_match == nullptr) {
            source_match = &reference;
        }
        if (!ascii_name_equal(reference.name, target)) {
            continue;
        }
        if (named_match != nullptr
            && named_match->target != reference.target) {
            ambiguous_name = true;
        } else if (named_match == nullptr) {
            named_match = &reference;
        }
    }
    if (named_match != nullptr && !ambiguous_name) {
        return named_match;
    }
    return ambiguous_source ? nullptr : source_match;
}

} // namespace

std::optional<HierarchyCompiledOccurrence> resolve_compiled_occurrence(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::CompiledInstanceView instance) noexcept
{
    const auto hir = hir_instance_identity(instance);
    if (!hir || !hir->instance.valid() || !hir->scope.valid()) {
        return std::nullopt;
    }
    const auto& design = specialization.design();
    if (hir->instance.value() >= design.semantics.instances().size()
        || hir->scope.value() >= design.semantics.scopes().size()) {
        return std::nullopt;
    }
    const auto& semantic_instance =
        design.semantics.instances()[hir->instance.value()];
    // Language HIR builders may reuse an existing semantic instance while
    // retaining a language-specific origin for the structural HIR record.
    // The stable occurrence identity is its ID, scope, source, and name; keep
    // the HIR origin below for direct source provenance.
    if (semantic_instance.id != hir->instance
        || semantic_instance.scope != hir->scope
        || semantic_instance.source != hir->source
        || semantic_instance.name != hir->name) {
        return std::nullopt;
    }
    const auto& semantic_scope =
        design.semantics.scopes()[hir->scope.value()];
    if (semantic_scope.id != hir->scope
        || !semantic_scope.unit.valid()) {
        return std::nullopt;
    }

    const semantic::CompiledReference* reference = nullptr;
    std::optional<semantic::CompiledUnitView> linked_target;
    if (hir->linkable) {
        reference = find_instance_reference(
            design, *hir, semantic_scope.unit);
        if (reference != nullptr && reference->target) {
            linked_target = design.find_unit(*reference->target);
        }
    }
    return HierarchyCompiledOccurrence {
        hir->instance,
        hir->scope,
        semantic_scope.unit,
        hir->source,
        hir->origin,
        hir->name,
        hir->target,
        reference,
        linked_target,
    };
}

} // namespace fsim::elaboration::elaboration_detail
