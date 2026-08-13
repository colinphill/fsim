// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/sdf_artifact_identity.hpp"

#include "fsim/compiler/object_cache.hpp"

#include <algorithm>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fsim::app {
namespace {
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::SourceSpan;

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message, const SourceSpan& span = { })
    {
        diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), span, { } });
    }

    bool checksum_spelling(const std::string_view value)
    {
        return value.size() == 64U
            && std::ranges::all_of(value, [](const unsigned char character) {
                   return (character >= '0' && character <= '9')
                       || (character >= 'a' && character <= 'f');
               });
    }

    template <typename Value>
    void add_number(compiler::CacheKeyBuilder& key, const std::string_view label,
        const Value value)
    {
        key.add(label, std::to_string(static_cast<std::uint64_t>(value)));
    }

    std::string make_endpoint_identity(const SdfResolvedNodeEndpoints& node,
        const SdfResolvedEndpoint& endpoint)
    {
        compiler::CacheKeyBuilder key;
        key.add("sdf-object-schema", "1");
        add_number(key, "node", node.node_id);
        add_number(key, "cell", node.cell_id);
        add_number(key, "construct", node.construct_kind);
        key.add("target", node.target_instance_path);
        add_number(key, "role", endpoint.role);
        add_number(key, "kind", endpoint.object_kind);
        key.add("instance", endpoint.instance_path);
        key.add("object", endpoint.object_path);
        add_number(key, "signal", endpoint.signal);
        add_number(key, "width", endpoint.object_width);
        add_number(key, "select-present", endpoint.select.has_value());
        if (endpoint.select) {
            add_number(key, "select-left", endpoint.select->left);
            add_number(key, "select-right", endpoint.select->right);
            add_number(key, "select-width", endpoint.select->width);
        }
        add_number(key, "language", endpoint.language);
        add_number(key, "direction", endpoint.direction);
        add_number(key, "conversion-present", endpoint.conversion.has_value());
        if (endpoint.conversion)
            add_number(key, "conversion", *endpoint.conversion);
        add_number(key, "peer-present", endpoint.conversion_peer.has_value());
        if (endpoint.conversion_peer)
            add_number(key, "peer", *endpoint.conversion_peer);
        key.add("edge", endpoint.edge_identity);
        key.add("condition", endpoint.condition_identity);
        return key.finish();
    }

    void sort_unique(std::vector<std::string>& values)
    {
        std::ranges::sort(values);
        values.erase(std::ranges::unique(values).begin(), values.end());
    }

    std::size_t identity_bytes(const artifact::DesignSdfAnnotation& annotation)
    {
        auto result = annotation.revision.size() + annotation.revision_adapter.size()
            + annotation.timescale.size() + annotation.selection_policy.size()
            + annotation.scope_identity.size() + annotation.source_digest.size()
            + annotation.design_digest.size() + annotation.ir_identity.size()
            + annotation.resolution_identity.size()
            + annotation.mapping_identity.size();
        const auto add = [&](const auto& values) {
            for (const auto& value : values)
                result += value.size();
        };
        add(annotation.selected_root_identities);
        add(annotation.semantic_unit_identities);
        add(annotation.semantic_object_identities);
        return result;
    }

    std::string make_annotation_cache_key(
        const artifact::DesignSdfAnnotation& annotation)
    {
        compiler::CacheKeyBuilder key;
        key.add("sdf-artifact-cache-schema", "1");
        key.add("revision", annotation.revision);
        key.add("adapter", annotation.revision_adapter);
        key.add("timescale-present", annotation.has_timescale ? "1" : "0");
        key.add("timescale", annotation.timescale);
        key.add("selection", annotation.selection_policy);
        key.add("scope", annotation.scope_identity);
        key.add("source", annotation.source_digest);
        key.add("design", annotation.design_digest);
        key.add("ir", annotation.ir_identity);
        key.add("resolution", annotation.resolution_identity);
        key.add("mapping", annotation.mapping_identity);
        for (const auto& identity : annotation.selected_root_identities)
            key.add("selected-root", identity);
        for (const auto& identity : annotation.semantic_unit_identities)
            key.add("semantic-unit", identity);
        for (const auto& identity : annotation.semantic_object_identities)
            key.add("semantic-object", identity);
        return key.finish();
    }

    std::string compose_key(const std::string_view label,
        const std::string_view base, const std::string_view sdf)
    {
        compiler::CacheKeyBuilder key;
        key.add("schema", "fsim-sdf-composed-cache-v1");
        key.add("kind", label);
        key.add("base", base);
        key.add("sdf", sdf);
        return key.finish();
    }
} // namespace

const char* to_string(const SdfDelaySelectionPolicy policy) noexcept
{
    switch (policy) {
    case SdfDelaySelectionPolicy::Minimum:
        return "min";
    case SdfDelaySelectionPolicy::Typical:
        return "typ";
    case SdfDelaySelectionPolicy::Maximum:
        return "max";
    }
    return "typ";
}

std::string compute_sdf_artifact_cache_key(
    const artifact::DesignSdfAnnotation& annotation)
{
    return make_annotation_cache_key(annotation);
}

std::string compute_sdf_semantic_object_identity(
    const SdfResolvedNodeEndpoints& node, const SdfResolvedEndpoint& endpoint)
{
    return make_endpoint_identity(node, endpoint);
}

SdfArtifactIdentityResult build_sdf_artifact_identity(
    const SdfSchemaSnapshot& schema, const SdfAnnotationSummary& summary,
    std::string design_digest, const SdfDelaySelectionPolicy selection_policy,
    const SdfArtifactIdentityLimits limits)
{
    SdfArtifactIdentityResult result;
    const auto& endpoints = summary.endpoint_resolution();
    const auto cells = endpoints ? endpoints->cells() : nullptr;
    const auto scope = cells ? cells->scope() : nullptr;
    const auto ir = scope ? scope->normalized_ir() : nullptr;
    if (!endpoints || !cells || !scope || !ir
        || schema.ir_semantic_identity() != ir->semantic_identity()
        || schema.resolution_semantic_identity()
            != endpoints->semantic_identity()
        || schema.summary_semantic_identity() != summary.semantic_identity()
        || !checksum_spelling(schema.source_checksum())
        || !checksum_spelling(design_digest)) {
        diagnose(result.diagnostics, "FSIM-SDF-ARTIFACT-001",
            "SDF artifact identity requires one complete compatible schema, "
            "scope, resolution, mapping, source digest, and design digest",
            schema.source_span());
        return result;
    }

    artifact::DesignSdfAnnotation annotation;
    annotation.revision = frontend::to_string(schema.revision());
    annotation.revision_adapter
        = frontend::to_string(schema.revision_adapter());
    annotation.has_timescale = ir->timescale().has_value();
    if (ir->timescale())
        annotation.timescale = ir->timescale()->canonical;
    annotation.selection_policy = to_string(selection_policy);
    annotation.scope_identity = scope->semantic_identity();
    annotation.source_digest = std::string { schema.source_checksum() };
    annotation.design_digest = std::move(design_digest);
    annotation.ir_identity = std::string { ir->semantic_identity() };
    annotation.resolution_identity
        = std::string { endpoints->semantic_identity() };
    annotation.mapping_identity = std::string { summary.semantic_identity() };
    for (const auto& root : scope->roots())
        annotation.selected_root_identities.push_back(root.selected_identity);
    for (const auto& cell : cells->cells()) {
        for (const auto& target : cell.targets)
            annotation.semantic_unit_identities.push_back(target.unit_identity);
    }
    for (const auto& node : endpoints->nodes()) {
        for (const auto& endpoint : node.endpoints) {
            annotation.semantic_object_identities.push_back(
                compute_sdf_semantic_object_identity(node, endpoint));
        }
    }
    sort_unique(annotation.selected_root_identities);
    sort_unique(annotation.semantic_unit_identities);
    sort_unique(annotation.semantic_object_identities);
    if (annotation.selected_root_identities.empty()
        || annotation.semantic_unit_identities.empty()
        || annotation.semantic_object_identities.empty()
        || annotation.selected_root_identities.size()
            > limits.max_selected_roots
        || annotation.semantic_unit_identities.size()
            > limits.max_semantic_units
        || annotation.semantic_object_identities.size()
            > limits.max_semantic_objects
        || identity_bytes(annotation) > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-ARTIFACT-003",
            "SDF artifact identity exceeds its selected-root, semantic-unit, "
            "semantic-object, or identity-byte budget",
            schema.source_span());
        return result;
    }
    annotation.cache_key = compute_sdf_artifact_cache_key(annotation);
    result.annotation = std::move(annotation);
    return result;
}

SdfArtifactApplyResult apply_sdf_artifact_identity(
    const artifact::DesignMetadata& metadata,
    artifact::DesignSdfAnnotation annotation)
{
    SdfArtifactApplyResult result;
    if (metadata.format != artifact::kDesignFormatVersion
        || !checksum_spelling(metadata.cache_key)
        || !checksum_spelling(metadata.design_digest)
        || annotation.cache_key != compute_sdf_artifact_cache_key(annotation)) {
        diagnose(result.diagnostics, "FSIM-SDF-ARTIFACT-001",
            "cannot attach an incomplete, stale, or noncanonical SDF identity "
            "to portable design metadata");
        return result;
    }
    if (std::ranges::any_of(metadata.sdf_annotations,
            [&](const auto& existing) {
                return existing.cache_key == annotation.cache_key
                    || existing.scope_identity == annotation.scope_identity;
            })) {
        diagnose(result.diagnostics, "FSIM-SDF-ARTIFACT-002",
            "portable design metadata contains a duplicate or conflicting SDF "
            "annotation identity");
        return result;
    }
    if (annotation.design_digest != metadata.design_digest) {
        diagnose(result.diagnostics, "FSIM-SDF-ARTIFACT-001",
            "SDF annotation identity was resolved against a stale portable "
            "design digest");
        return result;
    }
    auto updated = metadata;
    updated.cache_key
        = compose_key("design", metadata.cache_key, annotation.cache_key);
    for (auto& key : updated.specialization_cache_keys)
        key = compose_key("specialization", key, annotation.cache_key);
    updated.sdf_annotations.push_back(std::move(annotation));
    std::ranges::sort(updated.sdf_annotations, { },
        &artifact::DesignSdfAnnotation::cache_key);
    updated.design_digest = artifact::compute_design_digest(updated);
    result.metadata = std::move(updated);
    return result;
}

} // namespace fsim::app
