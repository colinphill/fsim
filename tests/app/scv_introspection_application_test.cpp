// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_extensions.hpp"

#include <cassert>

int main()
{
    using namespace fsim::systemc;

    ScvNativeSmartPtrRegistry registry;
    fsim::diagnostic::Engine diagnostics;
    const ScvObjectId object { 12U, 173U };
    const auto handle = registry.create(ScvNativeValueKind::introspection,
        object, "application.introspection", 12U, diagnostics);
    assert(handle && !diagnostics.has_error());
    const auto snapshot = capture_scv_extensions(
        registry, *handle, { }, diagnostics);
    assert(snapshot && snapshot->object == object);
    assert(snapshot->nodes.size() == 13U);
    assert(snapshot->nodes.front().kind == ScvNativeExtensionKind::record);
    assert(snapshot->nodes.back().kind == ScvNativeExtensionKind::logic_vector);
    assert(snapshot->nodes.back().bval_words.size() == 4U);
}
