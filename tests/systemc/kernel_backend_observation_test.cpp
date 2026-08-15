// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_observation.hpp"

#include <systemc>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace {

struct Identities {
    fsim::systemc::SystemCIslandId island;
    fsim::systemc::SystemCHierarchyId hierarchy;
    fsim::systemc::SystemCEndpointId custom;
    fsim::systemc::SystemCEndpointId initiator;
    fsim::systemc::SystemCEndpointId target;
};

Identities make_identities()
{
    using namespace fsim;
    diagnostic::Engine diagnostics;
    const systemc::SystemCKernelProtocolLimits limits;
    const auto island = systemc::make_systemc_island_id(
        "observation-island", limits, diagnostics);
    const auto hierarchy = systemc::make_systemc_hierarchy_id(
        *island, "work.top", limits, diagnostics);
    const auto object = systemc::make_systemc_object_id(
        *hierarchy, "work.top.native", limits, diagnostics);
    const auto custom = systemc::make_systemc_endpoint_id(
        *object, "channel", limits, diagnostics);
    const auto initiator = systemc::make_systemc_endpoint_id(
        *object, "initiator", limits, diagnostics);
    const auto target = systemc::make_systemc_endpoint_id(
        *object, "target", limits, diagnostics);
    assert(island && hierarchy && object && custom && initiator && target);
    assert(!diagnostics.has_error());
    return { *island, *hierarchy, *custom, *initiator, *target };
}

fsim::systemc::SystemCKernelChannelInventorySnapshot make_inventory(
    const Identities& ids)
{
    using namespace fsim;
    systemc::SystemCKernelChannelInventory inventory {
        ids.island, ids.hierarchy
    };
    diagnostic::Engine diagnostics;
    assert(inventory.register_channel(
        { "work.top.native", "custom_observable",
            systemc::SystemCKernelChannelKind::custom, std::nullopt,
            systemc::SystemCKernelWriterPolicy::none,
            systemc::SystemCKernelUpdateOwner::custom,
            systemc::SystemCKernelObservationMode::unsupported, false },
        diagnostics));
    assert(inventory.freeze(diagnostics));
    assert(inventory.snapshot().channels.front().channel == ids.custom);
    return inventory.snapshot();
}

fsim::systemc::SystemCKernelValue scalar(const std::uint64_t bits)
{
    fsim::diagnostic::Engine diagnostics;
    auto value = fsim::systemc::make_systemc_kernel_scalar_value(
        { bits, 32U, false }, { }, diagnostics);
    assert(value && !diagnostics.has_error());
    return *value;
}

fsim::systemc::SystemCKernelExecutionOrder order(const Identities& ids,
    const std::uint64_t sequence,
    const fsim::systemc::SystemCAccelleraRegion region
    = fsim::systemc::SystemCAccelleraRegion::quiescent)
{
    fsim::diagnostic::Engine diagnostics;
    const auto id = fsim::systemc::make_systemc_sequence_id(
        ids.island, sequence, diagnostics);
    assert(id);
    return { 0U, 0U, region, ids.island, *id };
}

void test_safe_point_and_custom_adapter(const Identities& ids,
    fsim::systemc::SystemCKernelSafePointObserver& observer)
{
    using namespace fsim;
    diagnostic::Engine diagnostics;
    assert(observer.inventory().channels.size() == 1U);
    assert(!observer.inventory().channels.front().descriptor.supported);
    assert(observer.query_inventory(order(ids, 1U), diagnostics));
    const auto report = observer.report(order(ids, 2U), diagnostics);
    assert(report && report->find("channels=1") != std::string::npos);

    diagnostic::Engine unsupported_diagnostics;
    assert(!observer.read(
        ids.custom, order(ids, 3U), unsupported_diagnostics));
    assert(unsupported_diagnostics.has_error());

    auto current = scalar(7U);
    std::size_t reads { };
    std::size_t writes { };
    assert(observer.register_adapter(
        { ids.custom,
            [&](diagnostic::Engine&) -> std::optional<systemc::SystemCKernelValue> {
                ++reads;
                return current;
            },
            [&](const systemc::SystemCKernelValue& value,
                diagnostic::Engine&) {
                ++writes;
                current = value;
                return true;
            } },
        diagnostics));
    diagnostic::Engine duplicate_diagnostics;
    assert(!observer.register_adapter(
        { ids.custom,
            [&](diagnostic::Engine&) { return std::optional { current }; },
            { } },
        duplicate_diagnostics));

    diagnostic::Engine unsafe_diagnostics;
    assert(!observer.write(ids.custom, scalar(8U),
        order(ids, 4U, systemc::SystemCAccelleraRegion::evaluate),
        unsafe_diagnostics));
    assert(writes == 0U);
    assert(observer.write(
        ids.custom, scalar(9U), order(ids, 4U), diagnostics));
    const auto readback = observer.read(ids.custom, order(ids, 5U), diagnostics);
    assert(readback == scalar(9U));
    assert(reads == 1U && writes == 1U);
    diagnostic::Engine order_diagnostics;
    assert(!observer.write(
        ids.custom, scalar(10U), order(ids, 4U), order_diagnostics));
    assert(writes == 1U && current == scalar(9U));
}

void test_tlm_observation(const Identities& ids,
    fsim::systemc::SystemCKernelSafePointObserver& observer)
{
    using namespace fsim;
    diagnostic::Engine diagnostics;
    systemc::SystemCKernelTlm1Registry tlm1 { ids.island };
    assert(tlm1.register_endpoint(
        { ids.initiator, systemc::SystemCKernelTlm1InterfaceKind::transport,
            "request32,response32", { ids.target }, false },
        diagnostics));
    assert(tlm1.register_endpoint(
        { ids.target, systemc::SystemCKernelTlm1InterfaceKind::transport,
            "request32,response32", { ids.initiator }, false },
        diagnostics));
    const auto sequence1
        = systemc::make_systemc_sequence_id(ids.island, 10U, diagnostics);
    const auto transaction1 = tlm1.begin(ids.initiator, ids.target, *sequence1,
        systemc::SystemCKernelTlm1Operation::transport, scalar(11U), 0U, 0U,
        diagnostics);
    assert(transaction1);
    assert(observer.observe_tlm1(tlm1.transactions().front(),
        systemc::SystemCKernelObservationKind::tlm1_begin, diagnostics));
    assert(tlm1.transition(*transaction1,
        systemc::SystemCKernelTlm1State::completed, scalar(111U), diagnostics));
    assert(observer.observe_tlm1(tlm1.transactions().front(),
        systemc::SystemCKernelObservationKind::tlm1_end, diagnostics));

    systemc::SystemCKernelTlm2Registry tlm2 { ids.island };
    assert(tlm2.register_endpoint(
        { ids.initiator, systemc::SystemCKernelTlm2SocketKind::initiator, 32U,
            { ids.target }, false },
        diagnostics));
    assert(tlm2.register_endpoint(
        { ids.target, systemc::SystemCKernelTlm2SocketKind::target, 32U,
            { ids.initiator }, false },
        diagnostics));
    systemc::SystemCKernelTlm2Payload payload;
    payload.address = 0x40U;
    payload.command = systemc::SystemCKernelTlm2Command::read;
    payload.response = systemc::SystemCKernelTlm2Response::incomplete;
    payload.streaming_width = 4U;
    payload.data.resize(4U);
    const auto sequence2
        = systemc::make_systemc_sequence_id(ids.island, 11U, diagnostics);
    const auto transaction2 = tlm2.begin(ids.initiator, ids.target, *sequence2,
        systemc::SystemCKernelTlm2Operation::direct_memory,
        systemc::SystemCKernelTlm2Phase::none, payload, 0U, 0U, diagnostics);
    assert(transaction2);
    assert(observer.observe_tlm2(tlm2.transactions().front(),
        systemc::SystemCKernelObservationKind::tlm2_begin, diagnostics));
    payload.response = systemc::SystemCKernelTlm2Response::ok;
    const systemc::SystemCKernelTlm2Dmi dmi { 0U, 255U,
        systemc::SystemCKernelTlm2DmiAccess::read_write, 1000U, 2000U };
    assert(tlm2.update(*transaction2,
        systemc::SystemCKernelTlm2State::completed,
        systemc::SystemCKernelTlm2Phase::none,
        systemc::SystemCKernelTlm2Sync::completed, payload, 0U, 0U, dmi,
        diagnostics));
    assert(observer.observe_tlm2(tlm2.transactions().front(),
        systemc::SystemCKernelObservationKind::tlm2_dmi, diagnostics));

    payload.response = systemc::SystemCKernelTlm2Response::incomplete;
    const auto phase_sequence
        = systemc::make_systemc_sequence_id(ids.island, 12U, diagnostics);
    const auto phase_transaction = tlm2.begin(ids.initiator, ids.target,
        *phase_sequence,
        systemc::SystemCKernelTlm2Operation::nonblocking_forward,
        systemc::SystemCKernelTlm2Phase::begin_request, payload, 0U, 0U,
        diagnostics);
    assert(phase_transaction);
    assert(observer.observe_tlm2(tlm2.transactions().back(),
        systemc::SystemCKernelObservationKind::tlm2_begin, diagnostics));
    assert(tlm2.update(*phase_transaction,
        systemc::SystemCKernelTlm2State::updated,
        systemc::SystemCKernelTlm2Phase::end_request,
        systemc::SystemCKernelTlm2Sync::updated, payload, 1000U, 0U,
        std::nullopt, diagnostics));
    assert(observer.observe_tlm2(tlm2.transactions().back(),
        systemc::SystemCKernelObservationKind::tlm2_phase, diagnostics));
    payload.response = systemc::SystemCKernelTlm2Response::ok;
    assert(tlm2.update(*phase_transaction,
        systemc::SystemCKernelTlm2State::completed,
        systemc::SystemCKernelTlm2Phase::begin_response,
        systemc::SystemCKernelTlm2Sync::completed, payload, 2000U, 0U,
        std::nullopt, diagnostics));
    assert(observer.observe_tlm2(tlm2.transactions().back(),
        systemc::SystemCKernelObservationKind::tlm2_end, diagnostics));

    payload.response = systemc::SystemCKernelTlm2Response::incomplete;
    const auto debug_sequence
        = systemc::make_systemc_sequence_id(ids.island, 13U, diagnostics);
    const auto debug_transaction = tlm2.begin(ids.initiator, ids.target,
        *debug_sequence, systemc::SystemCKernelTlm2Operation::debug_transport,
        systemc::SystemCKernelTlm2Phase::none, payload, 0U, 0U, diagnostics);
    assert(debug_transaction);
    payload.response = systemc::SystemCKernelTlm2Response::ok;
    assert(tlm2.update(*debug_transaction,
        systemc::SystemCKernelTlm2State::completed,
        systemc::SystemCKernelTlm2Phase::none,
        systemc::SystemCKernelTlm2Sync::completed, payload, 0U, 4U,
        std::nullopt, diagnostics));
    assert(observer.observe_tlm2(tlm2.transactions().back(),
        systemc::SystemCKernelObservationKind::tlm2_debug, diagnostics));

    const auto& records = observer.batch().records;
    assert(records.size() == 12U);
    assert(records[4].kind
        == systemc::SystemCKernelObservationKind::tlm1_begin);
    assert(records[4].transaction == *transaction1);
    assert(records[6].kind
        == systemc::SystemCKernelObservationKind::tlm2_begin);
    assert(records[7].kind == systemc::SystemCKernelObservationKind::tlm2_dmi);
    assert(!records[7].value && !records[7].transaction_bytes.empty());
    assert(records[9].kind
        == systemc::SystemCKernelObservationKind::tlm2_phase);
    assert(records[10].kind
        == systemc::SystemCKernelObservationKind::tlm2_end);
    assert(records[11].kind
        == systemc::SystemCKernelObservationKind::tlm2_debug);

    auto tlm1_bytes = records[5].transaction_bytes;
    const auto decoded1 = systemc::deserialize_systemc_kernel_tlm1_transaction(
        tlm1_bytes, { }, diagnostics);
    assert(decoded1 && decoded1->response == scalar(111U));
    const auto decoded2 = systemc::deserialize_systemc_kernel_tlm2_transaction(
        records[7].transaction_bytes, { }, diagnostics);
    assert(decoded2 && decoded2->dmi == dmi);
}

void test_codec_and_limits(const Identities& ids,
    const fsim::systemc::SystemCKernelSafePointObserver& observer)
{
    using namespace fsim;
    diagnostic::Engine diagnostics;
    const auto encoded = systemc::serialize_systemc_kernel_observation_batch(
        observer.batch(), { }, diagnostics);
    assert(encoded && !diagnostics.has_error());
    assert(systemc::deserialize_systemc_kernel_observation_batch(
               *encoded, { }, diagnostics)
        == observer.batch());

    auto truncated = *encoded;
    truncated.pop_back();
    diagnostic::Engine malformed_diagnostics;
    assert(!systemc::deserialize_systemc_kernel_observation_batch(
        truncated, { }, malformed_diagnostics));
    auto reserved = *encoded;
    reserved[28U] = std::byte { 1U };
    assert(!systemc::deserialize_systemc_kernel_observation_batch(
        reserved, { }, malformed_diagnostics));

    auto out_of_order = observer.batch();
    out_of_order.records.back().order.sequence.value = 1U;
    assert(!systemc::validate_systemc_kernel_observation_batch(
        out_of_order, { }, malformed_diagnostics));
    auto uncorrelated = observer.batch();
    uncorrelated.records[4].transaction.low ^= 1U;
    assert(!systemc::validate_systemc_kernel_observation_batch(
        uncorrelated, { }, malformed_diagnostics));

    systemc::SystemCKernelObservationLimits one;
    one.max_records = 1U;
    systemc::SystemCKernelSafePointObserver bounded {
        make_inventory(ids), one
    };
    diagnostic::Engine resource_diagnostics;
    assert(bounded.query_inventory(order(ids, 1U), resource_diagnostics));
    assert(!bounded.report(order(ids, 2U), resource_diagnostics));
}

} // namespace

int sc_main(int, char**)
{
    const auto ids = make_identities();
    fsim::systemc::SystemCKernelSafePointObserver observer {
        make_inventory(ids)
    };
    test_safe_point_and_custom_adapter(ids, observer);
    test_tlm_observation(ids, observer);
    test_codec_and_limits(ids, observer);
    return 0;
}
