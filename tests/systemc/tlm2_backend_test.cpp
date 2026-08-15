// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_tlm2.hpp"

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/tlm_quantumkeeper.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace {

struct NativeExtension final : tlm::tlm_extension<NativeExtension> {
    std::uint32_t tag { };

    [[nodiscard]] tlm::tlm_extension_base* clone() const override
    {
        auto* const result = new NativeExtension;
        result->tag = tag;
        return result;
    }

    void copy_from(const tlm::tlm_extension_base& other) override
    {
        tag = static_cast<const NativeExtension&>(other).tag;
    }
};

class NativeTarget final : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<NativeTarget, 32> socket { "socket" };
    std::array<unsigned char, 64> memory { };
    std::vector<tlm::tlm_phase> phases;
    std::size_t blocking_count { };
    std::size_t debug_count { };

    explicit NativeTarget(sc_core::sc_module_name name)
        : sc_core::sc_module { name }
    {
        socket.register_b_transport(this, &NativeTarget::blocking_transport);
        socket.register_nb_transport_fw(
            this, &NativeTarget::nonblocking_forward);
        socket.register_get_direct_mem_ptr(this, &NativeTarget::direct_memory);
        socket.register_transport_dbg(this, &NativeTarget::debug_transport);
    }

private:
    void blocking_transport(
        tlm::tlm_generic_payload& payload, sc_core::sc_time& delay)
    {
        ++blocking_count;
        assert(payload.get_address() + payload.get_data_length() <= memory.size());
        auto* extension = payload.get_extension<NativeExtension>();
        assert(extension != nullptr && extension->tag == 17U);
        auto* const data = payload.get_data_ptr();
        if (payload.is_write()) {
            std::memcpy(memory.data() + payload.get_address(), data,
                payload.get_data_length());
        } else {
            std::memcpy(data, memory.data() + payload.get_address(),
                payload.get_data_length());
        }
        delay += sc_core::sc_time { 2.0, sc_core::SC_NS };
        payload.set_dmi_allowed(true);
        payload.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    tlm::tlm_sync_enum nonblocking_forward(tlm::tlm_generic_payload& payload,
        tlm::tlm_phase& phase, sc_core::sc_time& delay)
    {
        phases.push_back(phase);
        assert(phase == tlm::BEGIN_REQ);
        phase = tlm::END_REQ;
        delay += sc_core::sc_time { 1.0, sc_core::SC_NS };
        tlm::tlm_phase backward_phase = tlm::BEGIN_RESP;
        auto backward_delay = delay;
        assert(socket->nb_transport_bw(payload, backward_phase, backward_delay)
            == tlm::TLM_COMPLETED);
        payload.set_response_status(tlm::TLM_OK_RESPONSE);
        return tlm::TLM_UPDATED;
    }

    bool direct_memory(
        tlm::tlm_generic_payload&, tlm::tlm_dmi& descriptor)
    {
        descriptor.set_dmi_ptr(memory.data());
        descriptor.set_start_address(0U);
        descriptor.set_end_address(memory.size() - 1U);
        descriptor.allow_read_write();
        descriptor.set_read_latency(sc_core::sc_time { 1.0, sc_core::SC_NS });
        descriptor.set_write_latency(sc_core::sc_time { 2.0, sc_core::SC_NS });
        socket->invalidate_direct_mem_ptr(16U, 31U);
        return true;
    }

    unsigned int debug_transport(tlm::tlm_generic_payload& payload)
    {
        ++debug_count;
        const auto count = std::min<std::size_t>(
            payload.get_data_length(), memory.size() - payload.get_address());
        std::memcpy(payload.get_data_ptr(), memory.data() + payload.get_address(),
            count);
        return static_cast<unsigned int>(count);
    }
};

class NativeInitiator final : public sc_core::sc_module {
public:
    tlm_utils::simple_initiator_socket<NativeInitiator, 32> socket { "socket" };
    std::vector<tlm::tlm_phase> backward_phases;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> invalidations;
    std::array<unsigned char, 4> readback { };
    std::array<unsigned char, 4> debug_readback { };
    bool dmi_read_write { };
    sc_core::sc_time local_time;

    explicit NativeInitiator(sc_core::sc_module_name name)
        : sc_core::sc_module { name }
    {
        socket.register_nb_transport_bw(
            this, &NativeInitiator::nonblocking_backward);
        socket.register_invalidate_direct_mem_ptr(
            this, &NativeInitiator::invalidate_direct_memory);
        SC_THREAD(run);
    }

private:
    tlm::tlm_sync_enum nonblocking_backward(tlm::tlm_generic_payload&,
        tlm::tlm_phase& phase, sc_core::sc_time&)
    {
        backward_phases.push_back(phase);
        assert(phase == tlm::BEGIN_RESP);
        return tlm::TLM_COMPLETED;
    }

    void invalidate_direct_memory(
        const sc_dt::uint64 start, const sc_dt::uint64 end)
    {
        invalidations.emplace_back(start, end);
    }

    static void configure_payload(tlm::tlm_generic_payload& result,
        unsigned char* data,
        const std::size_t size, const tlm::tlm_command command,
        NativeExtension& extension)
    {
        result.set_address(8U);
        result.set_command(command);
        result.set_data_ptr(data);
        result.set_data_length(static_cast<unsigned int>(size));
        result.set_streaming_width(static_cast<unsigned int>(size));
        result.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        result.set_extension(&extension);
    }

    void run()
    {
        tlm_utils::tlm_quantumkeeper::set_global_quantum(
            sc_core::sc_time { 10.0, sc_core::SC_NS });
        tlm_utils::tlm_quantumkeeper quantum;
        quantum.reset();
        NativeExtension extension;
        extension.tag = 17U;
        std::array<unsigned char, 4> written { 1U, 2U, 3U, 4U };
        tlm::tlm_generic_payload write;
        configure_payload(write, written.data(), written.size(),
            tlm::TLM_WRITE_COMMAND, extension);
        auto delay = sc_core::SC_ZERO_TIME;
        socket->b_transport(write, delay);
        assert(write.is_response_ok() && write.is_dmi_allowed());
        quantum.inc(delay);

        tlm::tlm_generic_payload read;
        configure_payload(read, readback.data(), readback.size(),
            tlm::TLM_READ_COMMAND, extension);
        delay = sc_core::SC_ZERO_TIME;
        socket->b_transport(read, delay);
        assert(read.is_response_ok());
        quantum.inc(delay);

        tlm::tlm_generic_payload nonblocking;
        configure_payload(nonblocking, readback.data(), readback.size(),
            tlm::TLM_READ_COMMAND, extension);
        tlm::tlm_phase phase = tlm::BEGIN_REQ;
        delay = sc_core::SC_ZERO_TIME;
        assert(socket->nb_transport_fw(nonblocking, phase, delay)
            == tlm::TLM_UPDATED);
        assert(phase == tlm::END_REQ);
        quantum.inc(delay);

        tlm::tlm_dmi dmi;
        assert(socket->get_direct_mem_ptr(read, dmi));
        dmi_read_write = dmi.is_read_allowed() && dmi.is_write_allowed();
        assert(dmi.get_start_address() == 0U && dmi.get_end_address() == 63U);

        tlm::tlm_generic_payload debug;
        configure_payload(debug, debug_readback.data(), debug_readback.size(),
            tlm::TLM_READ_COMMAND, extension);
        assert(socket->transport_dbg(debug) == debug_readback.size());
        local_time = quantum.get_local_time();
        const sc_core::sc_time expected_time { 5.0, sc_core::SC_NS };
        assert(local_time == expected_time);
        assert(quantum.need_sync() == false);
        write.clear_extension<NativeExtension>();
        read.clear_extension<NativeExtension>();
        nonblocking.clear_extension<NativeExtension>();
        debug.clear_extension<NativeExtension>();
        quantum.sync();
        sc_core::sc_stop();
    }
};

class NativeRoot final : public sc_core::sc_module {
public:
    NativeTarget target { "target" };
    NativeInitiator initiator { "initiator" };

    explicit NativeRoot(sc_core::sc_module_name name)
        : sc_core::sc_module { name }
    {
        initiator.socket.bind(target.socket);
    }
};

struct Identities {
    fsim::systemc::SystemCIslandId island;
    fsim::systemc::SystemCEndpointId initiator;
    fsim::systemc::SystemCEndpointId target;
};

Identities make_identities(const std::string_view suffix)
{
    using namespace fsim;
    diagnostic::Engine diagnostics;
    const systemc::SystemCKernelProtocolLimits limits;
    const auto island = systemc::make_systemc_island_id(
        "native-tlm2-island", limits, diagnostics);
    const auto hierarchy = systemc::make_systemc_hierarchy_id(
        *island, "work.top", limits, diagnostics);
    const auto object = systemc::make_systemc_object_id(
        *hierarchy, "work.top.native", limits, diagnostics);
    const auto initiator = systemc::make_systemc_endpoint_id(*object,
        std::string { "initiator." } + std::string { suffix }, limits,
        diagnostics);
    const auto target = systemc::make_systemc_endpoint_id(*object,
        std::string { "target." } + std::string { suffix }, limits,
        diagnostics);
    assert(island && hierarchy && object && initiator && target);
    return { *island, *initiator, *target };
}

fsim::systemc::SystemCKernelTlm2Payload bridge_payload()
{
    using namespace fsim::systemc;
    SystemCKernelTlm2Payload payload;
    payload.address = 0x1020U;
    payload.command = SystemCKernelTlm2Command::write;
    payload.response = SystemCKernelTlm2Response::incomplete;
    payload.dmi_allowed = true;
    payload.streaming_width = 4U;
    payload.data = { std::byte { 1U }, std::byte { 2U }, std::byte { 3U },
        std::byte { 4U } };
    payload.byte_enables = { std::byte { 0xffU } };
    payload.extensions = {
        { "example.route", { std::byte { 7U }, std::byte { 9U } } },
        { "example.tag", { std::byte { 17U } } },
    };
    return payload;
}

void test_registry_and_bridge()
{
    using namespace fsim;
    const auto ids = make_identities("bridge");
    systemc::SystemCKernelTlm2Registry registry { ids.island };
    diagnostic::Engine diagnostics;
    assert(registry.register_endpoint(
        { ids.initiator, systemc::SystemCKernelTlm2SocketKind::initiator, 32U,
            { ids.target }, true },
        diagnostics));
    assert(registry.register_endpoint(
        { ids.target, systemc::SystemCKernelTlm2SocketKind::target, 32U,
            { ids.initiator }, true },
        diagnostics));
    const auto sequence
        = systemc::make_systemc_sequence_id(ids.island, 1U, diagnostics);
    const auto transaction = registry.begin(ids.initiator, ids.target, *sequence,
        systemc::SystemCKernelTlm2Operation::nonblocking_forward,
        systemc::SystemCKernelTlm2Phase::begin_request, bridge_payload(), 0U,
        0U, diagnostics);
    assert(transaction);
    auto updated = bridge_payload();
    assert(registry.update(*transaction, systemc::SystemCKernelTlm2State::updated,
        systemc::SystemCKernelTlm2Phase::end_request,
        systemc::SystemCKernelTlm2Sync::updated, updated, 1000U, 0U,
        std::nullopt, diagnostics));
    updated.response = systemc::SystemCKernelTlm2Response::ok;
    assert(registry.update(*transaction,
        systemc::SystemCKernelTlm2State::completed,
        systemc::SystemCKernelTlm2Phase::begin_response,
        systemc::SystemCKernelTlm2Sync::completed, updated, 2000U, 0U,
        std::nullopt, diagnostics));
    const auto encoded = systemc::serialize_systemc_kernel_tlm2_transaction(
        registry.transactions().front(), { }, diagnostics);
    assert(encoded && !diagnostics.has_error());
    assert(systemc::deserialize_systemc_kernel_tlm2_transaction(
               *encoded, { }, diagnostics)
        == registry.transactions().front());

    auto truncated = *encoded;
    truncated.pop_back();
    diagnostic::Engine malformed_diagnostics;
    assert(!systemc::deserialize_systemc_kernel_tlm2_transaction(
        truncated, { }, malformed_diagnostics));
    auto reserved = *encoded;
    reserved[15U] = std::byte { 1U };
    assert(!systemc::deserialize_systemc_kernel_tlm2_transaction(
        reserved, { }, malformed_diagnostics));

    auto unordered = bridge_payload();
    std::ranges::reverse(unordered.extensions);
    assert(!systemc::validate_systemc_kernel_tlm2_payload(
        unordered, { }, malformed_diagnostics));
    assert(!registry.update(*transaction,
        systemc::SystemCKernelTlm2State::updated,
        systemc::SystemCKernelTlm2Phase::end_response,
        systemc::SystemCKernelTlm2Sync::updated, updated, 0U, 0U, std::nullopt,
        malformed_diagnostics));

    const auto native_ids = make_identities("native");
    systemc::SystemCKernelTlm2Registry native { native_ids.island };
    diagnostic::Engine native_diagnostics;
    assert(native.register_endpoint(
        { native_ids.initiator,
            systemc::SystemCKernelTlm2SocketKind::initiator, 64U,
            { native_ids.target }, false },
        native_diagnostics));
    assert(native.register_endpoint(
        { native_ids.target, systemc::SystemCKernelTlm2SocketKind::target, 64U,
            { native_ids.initiator }, false },
        native_diagnostics));
    const auto native_sequence = systemc::make_systemc_sequence_id(
        native_ids.island, 1U, native_diagnostics);
    const auto native_transaction = native.begin(native_ids.initiator,
        native_ids.target, *native_sequence,
        systemc::SystemCKernelTlm2Operation::blocking_transport,
        systemc::SystemCKernelTlm2Phase::none, bridge_payload(), 0U, 0U,
        native_diagnostics);
    assert(native_transaction);
    assert(!systemc::serialize_systemc_kernel_tlm2_transaction(
        native.transactions().front(), { }, native_diagnostics));
}

} // namespace

int sc_main(int, char**)
{
    NativeRoot root { "native" };
    sc_core::sc_start();
    assert(root.target.blocking_count == 2U);
    assert(root.target.debug_count == 1U);
    const std::vector<tlm::tlm_phase> expected_forward { tlm::BEGIN_REQ };
    assert(root.target.phases == expected_forward);
    const std::vector<tlm::tlm_phase> expected_backward { tlm::BEGIN_RESP };
    assert(root.initiator.backward_phases
        == expected_backward);
    const std::vector<std::pair<std::uint64_t, std::uint64_t>>
        expected_invalidations { { 16U, 31U } };
    assert(root.initiator.invalidations
        == expected_invalidations);
    const std::array<unsigned char, 4> expected_data { 1U, 2U, 3U, 4U };
    assert(root.initiator.readback == expected_data);
    assert(root.initiator.debug_readback == root.initiator.readback);
    assert(root.initiator.dmi_read_write);
    assert(sc_core::sc_time_stamp().to_string() == "5 ns");
    test_registry_and_bridge();
    return 0;
}
