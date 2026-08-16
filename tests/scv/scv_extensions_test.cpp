// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_extensions.hpp"

#include <cassert>
#include <cstdint>
#include <string_view>

namespace {

bool has_code(
    const fsim::diagnostic::Engine& diagnostics,
    const std::string_view code)
{
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

const fsim::systemc::ScvExtensionNode& named(
    const fsim::systemc::ScvExtensionSnapshot& snapshot,
    const std::string_view name)
{
    for (const auto& node : snapshot.nodes) {
        if (node.name == name) {
            return node;
        }
    }
    assert(false);
    return snapshot.nodes.front();
}

} // namespace

int main()
{
    using namespace fsim::systemc;

    ScvNativeSmartPtrRegistry registry;
    fsim::diagnostic::Engine diagnostics;
    const ScvObjectId object { 0x173U, 0x12U };
    const auto handle = registry.create(ScvNativeValueKind::introspection,
        object, "top.introspection", 1201U, diagnostics);
    assert(handle && !diagnostics.has_error());
    const auto snapshot = capture_scv_extensions(
        registry, *handle, { }, diagnostics);
    assert(snapshot && snapshot->object == object);
    assert(snapshot->nodes.size() == 13U);
    assert(snapshot->nodes.front().kind == ScvNativeExtensionKind::record);
    assert(snapshot->nodes.front().nominal_type.find("Introspection")
        != std::string::npos);

    const auto& enabled = named(*snapshot, "enabled");
    assert(enabled.kind == ScvNativeExtensionKind::boolean);
    assert(enabled.unsigned_value == 1U && enabled.range_left == 0U);
    const auto& mode = named(*snapshot, "mode");
    assert(mode.kind == ScvNativeExtensionKind::enumeration);
    assert(mode.enum_value == 1);
    assert(mode.enum_name.ends_with("running"));
    const auto& label = named(*snapshot, "label");
    assert(label.kind == ScvNativeExtensionKind::string);
    assert(label.string_value == "introspection-payload");
    const auto& nested = named(*snapshot, "nested");
    assert(nested.kind == ScvNativeExtensionKind::record);
    assert(nested.relation == ScvExtensionRelation::field);
    assert(nested.parent == 0U && nested.ordinal == 3U);
    const auto& samples = named(*snapshot, "samples");
    assert(samples.kind == ScvNativeExtensionKind::array);
    assert(samples.ordinal == 4U);
    assert(snapshot->nodes[8].parent == samples.id);
    assert(snapshot->nodes[8].relation == ScvExtensionRelation::element);
    assert(snapshot->nodes[8].ordinal == 0U);
    assert(snapshot->nodes[9].signed_value == -5);

    const auto& wide = named(*snapshot, "wide");
    assert(wide.bit_width == 257U && !wide.signed_type);
    assert(wide.range_left == 256U && wide.range_right == 0U);
    assert(wide.aval_words.size() == 5U && wide.bval_words.size() == 5U);
    assert((wide.aval_words[0] & UINT64_C(1)) != 0U);
    assert((wide.aval_words[2] & (UINT64_C(1) << 1U)) != 0U);
    assert((wide.aval_words[4] & UINT64_C(1)) != 0U);
    for (const auto word : wide.bval_words) {
        assert(word == 0U);
    }

    const auto& logic = named(*snapshot, "logic");
    assert(logic.kind == ScvNativeExtensionKind::logic_vector);
    assert(logic.bit_width == 193U && logic.range_left == 192U);
    assert(logic.aval_words.size() == 4U && logic.bval_words.size() == 4U);
    assert((logic.aval_words[0] & UINT64_C(1)) != 0U);
    assert((logic.bval_words[1] & UINT64_C(1)) != 0U);
    assert((logic.aval_words[2] & (UINT64_C(1) << 1U)) != 0U);
    assert((logic.bval_words[2] & (UINT64_C(1) << 1U)) != 0U);
    assert((logic.aval_words[3] & UINT64_C(1)) != 0U);
    assert((logic.bval_words[3] & UINT64_C(1)) == 0U);

    auto narrow_limits = ScvExtensionLimits { };
    narrow_limits.max_value_bits = 128U;
    fsim::diagnostic::Engine width_diagnostics;
    assert(!capture_scv_extensions(
        registry, *handle, narrow_limits, width_diagnostics));
    assert(has_code(width_diagnostics, "FSIM-SCV-X003"));
    auto shallow_limits = ScvExtensionLimits { };
    shallow_limits.max_depth = 1U;
    fsim::diagnostic::Engine depth_diagnostics;
    assert(!capture_scv_extensions(
        registry, *handle, shallow_limits, depth_diagnostics));
    assert(has_code(depth_diagnostics, "FSIM-SCV-X003"));
    auto node_limits = ScvExtensionLimits { };
    node_limits.max_nodes = 4U;
    fsim::diagnostic::Engine node_diagnostics;
    assert(!capture_scv_extensions(
        registry, *handle, node_limits, node_diagnostics));
    assert(has_code(node_diagnostics, "FSIM-SCV-X003"));
    auto text_limits = ScvExtensionLimits { };
    text_limits.max_total_string_bytes = 10U;
    fsim::diagnostic::Engine text_diagnostics;
    assert(!capture_scv_extensions(
        registry, *handle, text_limits, text_diagnostics));
    assert(has_code(text_diagnostics, "FSIM-SCV-X003"));

    assert(registry.release(*handle, diagnostics));
    fsim::diagnostic::Engine stale_diagnostics;
    assert(!capture_scv_extensions(
        registry, *handle, { }, stale_diagnostics));
    assert(has_code(stale_diagnostics, "FSIM-SCV-X001"));
}
