// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_binding_inventory.hpp"
#include "fsim/systemc/kernel_backend_binding_inventory_accellera.hpp"
#include "fsim/systemc/kernel_backend_inventory_accellera.hpp"

#include <systemc>

#include <array>
#include <cassert>
#include <cstddef>
#include <ranges>
#include <string_view>
#include <vector>

namespace {

class Child final : public sc_core::sc_module {
public:
    sc_core::sc_in<int> input { "input" };
    sc_core::sc_out<int> output { "output" };

    explicit Child(sc_core::sc_module_name name)
        : sc_core::sc_module { name }
    {
    }
};

class Parent final : public sc_core::sc_module {
public:
    sc_core::sc_in<int> input { "input" };
    sc_core::sc_out<int> output { "output" };
    Child child { "child" };

    explicit Parent(sc_core::sc_module_name name)
        : sc_core::sc_module { name }
    {
        child.input(input);
        child.output(output);
    }
};

class NativeRoot final : public sc_core::sc_module {
public:
    sc_core::sc_signal<int> first { "first" };
    sc_core::sc_signal<int> second { "second" };
    sc_core::sc_signal<int> result { "result" };
    Parent parent { "parent" };
    sc_core::sc_export<sc_core::sc_signal_in_if<int>> exported { "exported" };
    sc_core::sc_port<sc_core::sc_signal_in_if<int>, 0,
        sc_core::SC_ZERO_OR_MORE_BOUND>
        multi { "multi" };
    sc_core::sc_in<int> foreign { "foreign" };

    explicit NativeRoot(sc_core::sc_module_name name)
        : sc_core::sc_module { name }
    {
        parent.input(first);
        parent.output(result);
        exported.bind(first);
        multi.bind(first);
        multi.bind(second);
        foreign.bind(second);
    }
};

struct Fixture {
    fsim::systemc::SystemCKernelChannelInventory channels;
    fsim::systemc::SystemCEndpointId foreign;
};

Fixture make_fixture(const NativeRoot& root)
{
    using namespace fsim;
    diagnostic::Engine diagnostics;
    const systemc::SystemCKernelProtocolLimits limits;
    const auto island = systemc::make_systemc_island_id(
        "binding-inventory-island", limits, diagnostics);
    const auto hierarchy = systemc::make_systemc_hierarchy_id(
        *island, "native.top", limits, diagnostics);
    const auto foreign_object = systemc::make_systemc_object_id(
        *hierarchy, "vhdl.top.source", limits, diagnostics);
    const auto foreign = systemc::make_systemc_endpoint_id(
        *foreign_object, "output", limits, diagnostics);
    assert(island && hierarchy && foreign_object && foreign);
    systemc::SystemCKernelChannelInventory channels { *island, *hierarchy };
    assert(channels.register_channel(
        systemc::describe_accellera_channel(root.first), diagnostics));
    assert(channels.register_channel(
        systemc::describe_accellera_channel(root.second), diagnostics));
    assert(channels.register_channel(
        systemc::describe_accellera_channel(root.result), diagnostics));
    assert(channels.freeze(diagnostics));
    assert(!diagnostics.has_error());
    return { std::move(channels), *foreign };
}

std::vector<fsim::systemc::SystemCKernelBindingDescriptor> describe(
    const NativeRoot& root, const fsim::systemc::SystemCEndpointId foreign)
{
    using namespace fsim::systemc;
    std::array<std::string_view, 1> input_parent { root.parent.input.name() };
    std::array<std::string_view, 1> output_parent { root.parent.output.name() };
    auto child_input = describe_accellera_binding(root.parent.child.input,
        { make_accellera_binding_target(root.first, input_parent) });
    auto child_output = describe_accellera_binding(root.parent.child.output,
        { make_accellera_binding_target(root.result, output_parent) });
    auto mixed_target = make_accellera_binding_target(root.second);
    mixed_target.foreign_language = SystemCKernelHostLanguage::vhdl;
    mixed_target.foreign_endpoint = foreign;
    return { std::move(child_input),
        describe_accellera_binding(root.parent.input,
            { make_accellera_binding_target(root.first) }),
        std::move(child_output),
        describe_accellera_binding(root.parent.output,
            { make_accellera_binding_target(root.result) }),
        describe_accellera_binding(root.exported,
            { make_accellera_binding_target(root.first) }),
        describe_accellera_binding(root.multi,
            SystemCKernelBindingDirection::input,
            { make_accellera_binding_target(root.second),
                make_accellera_binding_target(root.first) }),
        describe_accellera_binding(root.foreign,
            { std::move(mixed_target) }) };
}

const fsim::systemc::SystemCKernelBindingEntry& find_binding(
    const fsim::systemc::SystemCKernelBindingInventorySnapshot& snapshot,
    const std::string_view suffix)
{
    const auto found = std::ranges::find_if(snapshot.bindings,
        [&](const auto& binding) {
            return std::string_view { binding.descriptor.declared_path }
                .ends_with(suffix);
        });
    assert(found != snapshot.bindings.end());
    return *found;
}

void test_binding_inventory(const NativeRoot& root)
{
    using namespace fsim;
    auto fixture = make_fixture(root);
    auto descriptors = describe(root, fixture.foreign);
    systemc::SystemCKernelBindingInventory inventory {
        fixture.channels.snapshot()
    };
    diagnostic::Engine diagnostics;
    for (auto& descriptor : descriptors) {
        assert(inventory.register_binding(std::move(descriptor), diagnostics));
    }
    assert(inventory.freeze(diagnostics));
    assert(inventory.frozen() && !diagnostics.has_error());
    assert(inventory.snapshot().bindings.size() == 7U);
    assert(std::ranges::is_sorted(inventory.snapshot().bindings, { },
        [](const auto& entry) { return entry.descriptor.declared_path; }));

    const auto& child_input = find_binding(inventory.snapshot(), "child.input");
    assert(child_input.descriptor.targets.front().chain.size() == 3U);
    assert(child_input.descriptor.targets.front().chain[1]
        == root.parent.input.name());
    const auto& exported = find_binding(inventory.snapshot(), ".exported");
    assert(exported.descriptor.kind
        == systemc::SystemCKernelBindingKind::export_interface);
    const auto& multi = find_binding(inventory.snapshot(), ".multi");
    assert(multi.descriptor.targets.size() == 2U);
    assert(multi.descriptor.targets[0].final_channel
        != multi.descriptor.targets[1].final_channel);
    const auto& mixed = find_binding(inventory.snapshot(), ".foreign");
    assert(mixed.descriptor.targets.front().foreign_language
        == systemc::SystemCKernelHostLanguage::vhdl);
    assert(mixed.descriptor.targets.front().foreign_endpoint == fixture.foreign);

    const auto encoded = systemc::serialize_systemc_kernel_binding_inventory(
        inventory.snapshot(), { }, diagnostics);
    assert(encoded && !diagnostics.has_error());
    assert(systemc::deserialize_systemc_kernel_binding_inventory(
               *encoded, { }, diagnostics)
        == inventory.snapshot());

    diagnostic::Engine lifecycle_diagnostics;
    assert(!inventory.freeze(lifecycle_diagnostics));
    auto truncated = *encoded;
    truncated.pop_back();
    assert(!systemc::deserialize_systemc_kernel_binding_inventory(
        truncated, { }, lifecycle_diagnostics));
    auto reserved = *encoded;
    reserved[44U] = std::byte { 1U };
    assert(!systemc::deserialize_systemc_kernel_binding_inventory(
        reserved, { }, lifecycle_diagnostics));
    auto unordered = inventory.snapshot();
    std::ranges::reverse(unordered.bindings);
    assert(!systemc::validate_systemc_kernel_binding_inventory(
        unordered, { }, lifecycle_diagnostics));
}

void test_negative_registration(const NativeRoot& root)
{
    using namespace fsim;
    auto fixture = make_fixture(root);
    diagnostic::Engine diagnostics;
    systemc::SystemCKernelBindingInventory inventory {
        fixture.channels.snapshot()
    };
    auto descriptor = describe(root, fixture.foreign).front();
    descriptor.targets.front().final_channel_path = "native.missing";
    descriptor.targets.front().chain.back() = "native.missing";
    assert(!inventory.register_binding(descriptor, diagnostics));
    assert(inventory.snapshot().bindings.empty());

    descriptor = describe(root, fixture.foreign).front();
    descriptor.targets.front().chain.insert(
        descriptor.targets.front().chain.begin() + 1,
        descriptor.declared_path);
    assert(!inventory.register_binding(descriptor, diagnostics));
    assert(inventory.snapshot().bindings.empty());

    systemc::SystemCKernelBindingLimits limits;
    limits.max_bindings = 1U;
    systemc::SystemCKernelBindingInventory bounded {
        fixture.channels.snapshot(), limits
    };
    auto descriptors = describe(root, fixture.foreign);
    assert(bounded.register_binding(std::move(descriptors[0]), diagnostics));
    assert(!bounded.register_binding(std::move(descriptors[1]), diagnostics));
}

} // namespace

int sc_main(int, char**)
{
    NativeRoot root { "native" };
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    test_binding_inventory(root);
    test_negative_registration(root);
    return 0;
}
