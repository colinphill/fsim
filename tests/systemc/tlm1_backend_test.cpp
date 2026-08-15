// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_tlm1.hpp"

#include <systemc>
#include <tlm>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace {

class AnalysisSink final : public sc_core::sc_module,
                           public tlm::tlm_analysis_if<std::uint32_t> {
public:
    explicit AnalysisSink(sc_core::sc_module_name name)
        : sc_core::sc_module { name }
    {
    }

    void write(const std::uint32_t& value) override
    {
        values.push_back(value);
    }

    std::vector<std::uint32_t> values;
};

class TransportTarget final : public sc_core::sc_module,
                              public tlm::tlm_transport_if<std::uint32_t,
                                  std::uint32_t> {
public:
    sc_core::sc_export<tlm::tlm_transport_if<std::uint32_t, std::uint32_t>>
        target { "target" };

    explicit TransportTarget(sc_core::sc_module_name name)
        : sc_core::sc_module { name }
    {
        target.bind(*this);
    }

    std::uint32_t transport(const std::uint32_t& request) override
    {
        requests.push_back(request);
        return request + 100U;
    }

    std::vector<std::uint32_t> requests;
};

class NativeTlmRoot final : public sc_core::sc_module {
public:
    explicit NativeTlmRoot(sc_core::sc_module_name name)
        : sc_core::sc_module { name }
    {
        put.bind(fifo);
        get_peek.bind(fifo);
        initiator.bind(target.target);
        analysis.bind(first_sink);
        analysis.bind(second_sink);
        SC_THREAD(produce);
        SC_THREAD(consume);
    }

    tlm::tlm_fifo<std::uint32_t> fifo { "fifo", 1 };
    sc_core::sc_port<tlm::tlm_put_if<std::uint32_t>> put { "put" };
    sc_core::sc_port<tlm::tlm_get_peek_if<std::uint32_t>> get_peek {
        "get_peek"
    };
    sc_core::sc_port<tlm::tlm_transport_if<std::uint32_t, std::uint32_t>>
        initiator { "initiator" };
    tlm::tlm_analysis_port<std::uint32_t> analysis { "analysis" };
    TransportTarget target { "transport_target" };
    AnalysisSink first_sink { "first_sink" };
    AnalysisSink second_sink { "second_sink" };
    std::vector<std::uint32_t> consumed;
    std::vector<std::string_view> order;
    sc_core::sc_time producer_unblocked;
    std::uint32_t transport_response { };

private:
    void produce()
    {
        assert(put->nb_can_put());
        assert(put->nb_put(11U));
        assert(!put->nb_can_put());
        order.push_back("put-11");
        put->put(22U);
        producer_unblocked = sc_core::sc_time_stamp();
        order.push_back("put-22");
    }

    void consume()
    {
        wait(5.0, sc_core::SC_NS);
        std::uint32_t value { };
        assert(get_peek->nb_can_peek());
        assert(get_peek->nb_peek(value) && value == 11U);
        order.push_back("peek-11");
        assert(get_peek->nb_get(value) && value == 11U);
        consumed.push_back(value);
        wait(sc_core::SC_ZERO_TIME);
        value = get_peek->get();
        consumed.push_back(value);
        transport_response = initiator->transport(7U);
        analysis.write(transport_response);
        sc_core::sc_stop();
    }
};

struct Identities {
    fsim::systemc::SystemCIslandId island;
    fsim::systemc::SystemCHierarchyId hierarchy;
    fsim::systemc::SystemCObjectId object;
    fsim::systemc::SystemCEndpointId source;
    fsim::systemc::SystemCEndpointId target;
};

Identities make_identities(const std::string_view suffix)
{
    using namespace fsim;
    diagnostic::Engine diagnostics;
    const systemc::SystemCKernelProtocolLimits limits;
    const auto island = systemc::make_systemc_island_id(
        "native-tlm1-island", limits, diagnostics);
    const auto hierarchy = systemc::make_systemc_hierarchy_id(
        *island, "work.top", limits, diagnostics);
    const auto object = systemc::make_systemc_object_id(
        *hierarchy, "work.top.native", limits, diagnostics);
    const auto source = systemc::make_systemc_endpoint_id(
        *object, std::string { "source." } + std::string { suffix }, limits,
        diagnostics);
    const auto target = systemc::make_systemc_endpoint_id(
        *object, std::string { "target." } + std::string { suffix }, limits,
        diagnostics);
    assert(island && hierarchy && object && source && target);
    assert(!diagnostics.has_error());
    return { *island, *hierarchy, *object, *source, *target };
}

fsim::systemc::SystemCKernelValue value(const std::uint64_t bits)
{
    fsim::diagnostic::Engine diagnostics;
    const auto result = fsim::systemc::make_systemc_kernel_scalar_value(
        { bits, 32U, false }, { }, diagnostics);
    assert(result && !diagnostics.has_error());
    return *result;
}

void test_registry_and_bridge()
{
    using namespace fsim;
    const auto ids = make_identities("transport");
    systemc::SystemCKernelTlm1Registry registry { ids.island };
    diagnostic::Engine diagnostics;
    assert(registry.register_endpoint(
        { ids.source, systemc::SystemCKernelTlm1InterfaceKind::transport,
            "request32,response32", { ids.target }, false },
        diagnostics));
    assert(registry.register_endpoint(
        { ids.target, systemc::SystemCKernelTlm1InterfaceKind::transport,
            "request32,response32", { ids.source }, false },
        diagnostics));
    const auto sequence
        = systemc::make_systemc_sequence_id(ids.island, 1U, diagnostics);
    const auto transaction = registry.begin(ids.source, ids.target, *sequence,
        systemc::SystemCKernelTlm1Operation::transport, value(7U), 0U, 0U,
        diagnostics);
    assert(transaction);
    assert(registry.transition(*transaction,
        systemc::SystemCKernelTlm1State::blocked, std::nullopt, diagnostics));
    assert(registry.transition(*transaction,
        systemc::SystemCKernelTlm1State::completed, value(107U), diagnostics));
    assert(registry.transactions().size() == 1U);
    assert(registry.transactions().front().response == value(107U));
    assert(!systemc::serialize_systemc_kernel_tlm1_transaction(
        registry.transactions().front(), { }, diagnostics));

    systemc::SystemCKernelTlm1Registry bridge { ids.island };
    diagnostics.clear();
    assert(bridge.register_endpoint(
        { ids.source, systemc::SystemCKernelTlm1InterfaceKind::transport,
            "request32,response32", { ids.target }, true },
        diagnostics));
    assert(bridge.register_endpoint(
        { ids.target, systemc::SystemCKernelTlm1InterfaceKind::transport,
            "request32,response32", { ids.source }, true },
        diagnostics));
    const auto bridge_sequence
        = systemc::make_systemc_sequence_id(ids.island, 2U, diagnostics);
    const auto bridge_transaction = bridge.begin(ids.source, ids.target,
        *bridge_sequence, systemc::SystemCKernelTlm1Operation::transport,
        value(9U), 5000U, 3U, diagnostics);
    assert(bridge_transaction);
    assert(bridge.transition(*bridge_transaction,
        systemc::SystemCKernelTlm1State::completed, value(109U), diagnostics));
    const auto encoded = systemc::serialize_systemc_kernel_tlm1_transaction(
        bridge.transactions().front(), { }, diagnostics);
    assert(encoded && !diagnostics.has_error());
    const auto decoded = systemc::deserialize_systemc_kernel_tlm1_transaction(
        *encoded, { }, diagnostics);
    assert(decoded == bridge.transactions().front());

    auto truncated = *encoded;
    truncated.pop_back();
    diagnostic::Engine malformed_diagnostics;
    assert(!systemc::deserialize_systemc_kernel_tlm1_transaction(
        truncated, { }, malformed_diagnostics));
    auto reserved = *encoded;
    reserved[13U] = std::byte { 1U };
    assert(!systemc::deserialize_systemc_kernel_tlm1_transaction(
        reserved, { }, malformed_diagnostics));

    auto tiny_limits = systemc::SystemCKernelTlm1Limits { };
    tiny_limits.max_transactions = 1U;
    systemc::SystemCKernelTlm1Registry bounded { ids.island, tiny_limits };
    assert(bounded.register_endpoint(
        { ids.source, systemc::SystemCKernelTlm1InterfaceKind::transport,
            "request32,response32", { ids.target }, false },
        diagnostics));
    assert(bounded.register_endpoint(
        { ids.target, systemc::SystemCKernelTlm1InterfaceKind::transport,
            "request32,response32", { ids.source }, false },
        diagnostics));
    assert(bounded.begin(ids.source, ids.target, *sequence,
        systemc::SystemCKernelTlm1Operation::transport, value(1U), 0U, 0U,
        diagnostics));
    const auto third_sequence
        = systemc::make_systemc_sequence_id(ids.island, 3U, diagnostics);
    assert(!bounded.begin(ids.source, ids.target, *third_sequence,
        systemc::SystemCKernelTlm1Operation::transport, value(2U), 0U, 0U,
        diagnostics));

    diagnostic::Engine endpoint_diagnostics;
    for (std::uint8_t raw = 1U; raw <= 9U; ++raw) {
        assert(systemc::validate_systemc_kernel_tlm1_endpoint(
            { ids.source,
                static_cast<systemc::SystemCKernelTlm1InterfaceKind>(raw),
                "payload32", { ids.target }, false },
            { }, endpoint_diagnostics));
    }
    assert(!systemc::validate_systemc_kernel_tlm1_endpoint(
        { ids.source, systemc::SystemCKernelTlm1InterfaceKind::fifo,
            "payload32", { ids.target, ids.target }, false },
        { }, endpoint_diagnostics));

    diagnostic::Engine lifecycle_diagnostics;
    assert(!registry.transition(*transaction,
        systemc::SystemCKernelTlm1State::blocked, std::nullopt,
        lifecycle_diagnostics));
    assert(!registry.register_endpoint(
        { ids.source, systemc::SystemCKernelTlm1InterfaceKind::transport,
            "request32,response32", { ids.target }, false },
        lifecycle_diagnostics));

    systemc::SystemCKernelTlm1Registry mismatched_bridge { ids.island };
    diagnostic::Engine mismatch_diagnostics;
    assert(mismatched_bridge.register_endpoint(
        { ids.source, systemc::SystemCKernelTlm1InterfaceKind::transport,
            "request32,response32", { ids.target }, true },
        mismatch_diagnostics));
    assert(mismatched_bridge.register_endpoint(
        { ids.target, systemc::SystemCKernelTlm1InterfaceKind::transport,
            "request32,response32", { ids.source }, false },
        mismatch_diagnostics));
    assert(!mismatched_bridge.begin(ids.source, ids.target, *sequence,
        systemc::SystemCKernelTlm1Operation::transport, value(3U), 0U, 0U,
        mismatch_diagnostics));

    systemc::SystemCKernelTlm1Registry one_way { ids.island };
    diagnostic::Engine one_way_diagnostics;
    assert(one_way.register_endpoint(
        { ids.source, systemc::SystemCKernelTlm1InterfaceKind::transport,
            "request32,response32", { ids.target }, false },
        one_way_diagnostics));
    const auto unrelated = make_identities("unrelated");
    assert(one_way.register_endpoint(
        { ids.target, systemc::SystemCKernelTlm1InterfaceKind::transport,
            "request32,response32", { unrelated.source }, false },
        one_way_diagnostics));
    assert(!one_way.begin(ids.source, ids.target, *sequence,
        systemc::SystemCKernelTlm1Operation::transport, value(4U), 0U, 0U,
        one_way_diagnostics));
}

} // namespace

int sc_main(int, char**)
{
    NativeTlmRoot root { "native" };
    sc_core::sc_start();
    assert(root.consumed == std::vector<std::uint32_t>({ 11U, 22U }));
    assert((root.producer_unblocked
        == sc_core::sc_time { 5.0, sc_core::SC_NS }));
    assert(root.transport_response == 107U);
    assert(root.target.requests == std::vector<std::uint32_t> { 7U });
    assert(root.first_sink.values == std::vector<std::uint32_t> { 107U });
    assert(root.second_sink.values == std::vector<std::uint32_t> { 107U });
    assert(root.order == std::vector<std::string_view>({ "put-11", "peek-11", "put-22" }));
    test_registry_and_bridge();
    return 0;
}
