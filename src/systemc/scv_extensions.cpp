// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_extensions.hpp"

#include <limits>
#include <utility>

namespace fsim::systemc {
namespace {

    bool valid_limits(
        const ScvExtensionLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (limits.max_nodes == 0U || limits.max_depth == 0U
            || limits.max_value_bits == 0U || limits.max_total_words == 0U
            || limits.max_string_bytes == 0U
            || limits.max_total_string_bytes == 0U
            || limits.max_nodes > std::numeric_limits<std::uint32_t>::max()
            || limits.max_depth > std::numeric_limits<std::uint32_t>::max()
            || limits.max_value_bits
                > std::numeric_limits<std::uint32_t>::max()
            || limits.max_total_words
                > std::numeric_limits<std::uint32_t>::max()
            || limits.max_string_bytes
                > std::numeric_limits<std::uint32_t>::max()
            || limits.max_total_string_bytes
                > std::numeric_limits<std::uint32_t>::max()) {
            diagnostics.error(
                "FSIM-SCV-X003", "SCV extension snapshot limits are inconsistent");
            return false;
        }
        return true;
    }

    struct PendingNode {
        std::vector<ScvNativeExtensionStep> path;
        std::optional<std::size_t> parent;
        ScvExtensionRelation relation { ScvExtensionRelation::root };
        std::size_t ordinal { };
        std::size_t depth { };
    };

} // namespace

std::optional<ScvExtensionSnapshot> capture_scv_extensions(
    const ScvNativeSmartPtrRegistry& registry,
    const ScvNativeSmartPtrHandle handle,
    const ScvExtensionLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)) {
        return std::nullopt;
    }
    const auto owner = registry.info(handle, diagnostics);
    if (!owner) {
        diagnostics.error(
            "FSIM-SCV-X001", "SCV extension snapshot requires a live native owner");
        return std::nullopt;
    }
    ScvExtensionSnapshot result;
    result.object = owner->object;
    std::vector<PendingNode> pending;
    pending.push_back({ });
    std::size_t total_words { };
    std::size_t total_string_bytes { };
    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        if (current.depth > limits.max_depth
            || result.nodes.size() >= limits.max_nodes) {
            diagnostics.error("FSIM-SCV-X003",
                "SCV extension hierarchy exceeds its depth or node limit");
            return std::nullopt;
        }
        const auto extension = registry.extension(
            handle, current.path, diagnostics);
        if (!extension) {
            diagnostics.error(
                "FSIM-SCV-X001", "SCV extension hierarchy changed during capture");
            return std::nullopt;
        }
        const auto node_string_bytes = static_cast<std::uint64_t>(
                                           extension->name.size())
            + static_cast<std::uint64_t>(extension->type_name.size())
            + static_cast<std::uint64_t>(extension->enum_name.size())
            + static_cast<std::uint64_t>(extension->string_value.size());
        if (extension->bit_width > limits.max_value_bits
            || extension->name.size() > limits.max_string_bytes
            || extension->type_name.size() > limits.max_string_bytes
            || extension->enum_name.size() > limits.max_string_bytes
            || extension->string_value.size() > limits.max_string_bytes
            || node_string_bytes
                > limits.max_total_string_bytes - total_string_bytes
            || extension->aval_words.size()
                > limits.max_total_words - total_words
            || extension->bval_words.size()
                > limits.max_total_words - total_words
                    - extension->aval_words.size()) {
            diagnostics.error("FSIM-SCV-X003",
                "SCV extension value or text exceeds its resource limit");
            return std::nullopt;
        }
        total_words += extension->aval_words.size() + extension->bval_words.size();
        total_string_bytes += static_cast<std::size_t>(node_string_bytes);
        ScvExtensionNode node;
        node.id = result.nodes.size();
        node.parent = current.parent;
        node.relation = current.relation;
        node.ordinal = current.ordinal;
        node.name = extension->name;
        node.nominal_type = extension->type_name;
        node.kind = extension->kind;
        node.bit_width = extension->bit_width;
        node.signed_type = extension->signed_type;
        if (extension->bit_width != 0U
            && extension->kind != ScvNativeExtensionKind::record
            && extension->kind != ScvNativeExtensionKind::array
            && extension->kind != ScvNativeExtensionKind::string) {
            node.range_left = extension->bit_width - 1U;
            node.range_right = 0U;
        }
        node.signed_value = extension->signed_value;
        node.unsigned_value = extension->unsigned_value;
        node.enum_value = extension->enum_value;
        node.enum_name = extension->enum_name;
        node.string_value = extension->string_value;
        node.aval_words = extension->aval_words;
        node.bval_words = extension->bval_words;
        const auto parent = node.id;
        result.nodes.push_back(std::move(node));

        for (std::size_t ordinal = extension->children; ordinal > 0U; --ordinal) {
            auto child_path = current.path;
            const auto child = ordinal - 1U;
            const auto record = extension->kind == ScvNativeExtensionKind::record;
            const auto array = extension->kind == ScvNativeExtensionKind::array;
            if (!record && !array) {
                diagnostics.error("FSIM-SCV-X002",
                    "SCV scalar extension reports child extensions");
                return std::nullopt;
            }
            child_path.push_back({ record
                    ? ScvNativeExtensionStepKind::field
                    : ScvNativeExtensionStepKind::element,
                child });
            pending.push_back({ std::move(child_path), parent,
                record ? ScvExtensionRelation::field
                       : ScvExtensionRelation::element,
                child, current.depth + 1U });
        }
    }
    return result;
}

} // namespace fsim::systemc
