// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_inventory.hpp"
#include "fsim/systemc/kernel_backend_inventory_accellera.hpp"

#include <systemc>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string_view>
#include <vector>

namespace {

class CustomChannel final : public sc_core::sc_prim_channel {
public:
    explicit CustomChannel(const char* name)
        : sc_core::sc_prim_channel { name }
    {
    }

    [[nodiscard]] const char* kind() const override
    {
        return "custom_observable";
    }
};

class NativeRoot final : public sc_core::sc_module {
public:
    sc_core::sc_signal<std::int32_t> signal { "signal" };
    sc_core::sc_buffer<sc_dt::sc_logic> buffer { "buffer" };
    sc_core::sc_clock clock { "clock", 10.0, sc_core::SC_NS };
    sc_core::sc_signal_resolved resolved { "resolved" };
    sc_core::sc_signal_rv<8> resolved_vector { "resolved_vector" };
    sc_core::sc_signal<sc_dt::sc_uint<17>> unsigned_wide { "unsigned_wide" };
    sc_core::sc_signal<sc_dt::sc_bigint<129>> signed_wide { "signed_wide" };
    sc_core::sc_mutex mutex { "mutex" };
    sc_core::sc_semaphore semaphore { "semaphore", 2 };
    sc_core::sc_event_queue event_queue { "event_queue" };
    CustomChannel custom { "custom" };

    explicit NativeRoot(sc_core::sc_module_name name)
        : sc_core::sc_module { name }
    {
    }
};

struct Identities {
    fsim::systemc::SystemCIslandId island;
    fsim::systemc::SystemCHierarchyId hierarchy;
};

Identities make_identities()
{
    using namespace fsim;
    diagnostic::Engine diagnostics;
    const systemc::SystemCKernelProtocolLimits limits;
    const auto island = systemc::make_systemc_island_id(
        "channel-inventory-island", limits, diagnostics);
    const auto hierarchy = systemc::make_systemc_hierarchy_id(
        *island, "native.top", limits, diagnostics);
    assert(island && hierarchy && !diagnostics.has_error());
    return { *island, *hierarchy };
}

std::vector<fsim::systemc::SystemCKernelChannelDescriptor> describe(
    const NativeRoot& root)
{
    using fsim::systemc::describe_accellera_channel;
    return { describe_accellera_channel(root.resolved_vector),
        describe_accellera_channel(root.signal),
        describe_accellera_channel(root.event_queue),
        describe_accellera_channel(root.clock),
        describe_accellera_channel(root.semaphore),
        describe_accellera_channel(root.buffer),
        describe_accellera_channel(root.resolved),
        describe_accellera_channel(root.mutex),
        describe_accellera_channel(root.unsigned_wide),
        describe_accellera_channel(root.signed_wide) };
}

const fsim::systemc::SystemCKernelChannelInventoryEntry& find_entry(
    const fsim::systemc::SystemCKernelChannelInventorySnapshot& snapshot,
    const std::string_view suffix)
{
    const auto found = std::ranges::find_if(snapshot.channels,
        [&](const auto& entry) {
            return std::string_view { entry.descriptor.canonical_path }
                .ends_with(suffix);
        });
    assert(found != snapshot.channels.end());
    return *found;
}

void test_inventory(const NativeRoot& root)
{
    using namespace fsim;
    const auto ids = make_identities();
    auto descriptors = describe(root);
    systemc::SystemCKernelChannelInventory inventory {
        ids.island, ids.hierarchy
    };
    diagnostic::Engine diagnostics;
    for (const auto& descriptor : descriptors) {
        assert(inventory.register_channel(descriptor, diagnostics));
    }
    assert(inventory.freeze(diagnostics));
    assert(inventory.frozen() && !diagnostics.has_error());
    assert(inventory.snapshot().channels.size() == 10U);
    assert(std::ranges::is_sorted(inventory.snapshot().channels, { },
        [](const auto& entry) { return entry.descriptor.canonical_path; }));

    const auto& signal = find_entry(inventory.snapshot(), ".signal");
    assert(signal.descriptor.kind
        == systemc::SystemCKernelChannelKind::signal);
    assert(signal.descriptor.value
        && signal.descriptor.value->width == 32U
        && signal.descriptor.value->is_signed);
    const auto& buffer = find_entry(inventory.snapshot(), ".buffer");
    assert(buffer.descriptor.kind
        == systemc::SystemCKernelChannelKind::buffer);
    assert(buffer.descriptor.value
        && buffer.descriptor.value->kind
            == systemc::SystemCKernelValueKind::logic4);
    assert(find_entry(inventory.snapshot(), ".clock").descriptor.observation
        == systemc::SystemCKernelObservationMode::clock_edges);
    const auto& resolved_vector
        = find_entry(inventory.snapshot(), ".resolved_vector");
    assert(resolved_vector.descriptor.value
        && resolved_vector.descriptor.value->width == 8U);
    assert(resolved_vector.descriptor.writer
        == systemc::SystemCKernelWriterPolicy::many);
    const auto& unsigned_wide
        = find_entry(inventory.snapshot(), ".unsigned_wide");
    assert(unsigned_wide.descriptor.value
        && unsigned_wide.descriptor.value->width == 17U
        && !unsigned_wide.descriptor.value->is_signed);
    const auto& signed_wide = find_entry(inventory.snapshot(), ".signed_wide");
    assert(signed_wide.descriptor.value
        && signed_wide.descriptor.value->width == 129U
        && signed_wide.descriptor.value->is_signed);
    assert(find_entry(inventory.snapshot(), ".mutex").descriptor.update_owner
        == systemc::SystemCKernelUpdateOwner::primitive_kernel);

    const auto encoded = systemc::serialize_systemc_kernel_channel_inventory(
        inventory.snapshot(), { }, diagnostics);
    assert(encoded && !diagnostics.has_error());
    assert(systemc::deserialize_systemc_kernel_channel_inventory(
               *encoded, { }, diagnostics)
        == inventory.snapshot());
    std::ranges::reverse(descriptors);
    assert(inventory.validate_live_snapshot(descriptors, diagnostics));

    auto missing = descriptors;
    missing.pop_back();
    diagnostic::Engine lifecycle_diagnostics;
    assert(!inventory.validate_live_snapshot(missing, lifecycle_diagnostics));
    assert(!inventory.register_channel(
        descriptors.front(), lifecycle_diagnostics));
    assert(!inventory.freeze(lifecycle_diagnostics));

    auto truncated = *encoded;
    truncated.pop_back();
    diagnostic::Engine malformed_diagnostics;
    assert(!systemc::deserialize_systemc_kernel_channel_inventory(
        truncated, { }, malformed_diagnostics));
    auto reserved = *encoded;
    reserved[44U] = std::byte { 1U };
    assert(!systemc::deserialize_systemc_kernel_channel_inventory(
        reserved, { }, malformed_diagnostics));
    auto unordered = inventory.snapshot();
    std::ranges::reverse(unordered.channels);
    assert(!systemc::validate_systemc_kernel_channel_inventory(
        unordered, { }, malformed_diagnostics));
}

void test_negative_registration(const NativeRoot& root)
{
    using namespace fsim;
    const auto ids = make_identities();
    systemc::SystemCKernelChannelInventory custom {
        ids.island, ids.hierarchy
    };
    diagnostic::Engine custom_diagnostics;
    assert(custom.register_channel(
        systemc::describe_accellera_channel(
            static_cast<const sc_core::sc_prim_channel&>(root.custom)),
        custom_diagnostics));
    assert(custom.freeze(custom_diagnostics));
    assert(custom.snapshot().channels.size() == 1U);
    assert(!custom.snapshot().channels.front().descriptor.supported);
    assert(custom.snapshot().channels.front().descriptor.observation
        == systemc::SystemCKernelObservationMode::unsupported);
    const auto encoded = systemc::serialize_systemc_kernel_channel_inventory(
        custom.snapshot(), { }, custom_diagnostics);
    assert(encoded);
    assert(systemc::deserialize_systemc_kernel_channel_inventory(
               *encoded, { }, custom_diagnostics)
        == custom.snapshot());

    systemc::SystemCKernelInventoryLimits one_limit;
    one_limit.max_channels = 1U;
    systemc::SystemCKernelChannelInventory bounded {
        ids.island, ids.hierarchy, one_limit
    };
    diagnostic::Engine resource_diagnostics;
    const auto descriptors = describe(root);
    assert(bounded.register_channel(descriptors[0], resource_diagnostics));
    assert(!bounded.register_channel(descriptors[1], resource_diagnostics));

    systemc::SystemCKernelChannelInventory duplicate {
        ids.island, ids.hierarchy
    };
    diagnostic::Engine duplicate_diagnostics;
    assert(duplicate.register_channel(descriptors[0], duplicate_diagnostics));
    assert(!duplicate.register_channel(descriptors[0], duplicate_diagnostics));
}

} // namespace

int sc_main(int, char**)
{
    NativeRoot root { "native" };
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    test_inventory(root);
    test_negative_registration(root);
    return 0;
}
