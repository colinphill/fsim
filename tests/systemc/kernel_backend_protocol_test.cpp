// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/kernel_backend_binding_inventory.hpp"
#include "fsim/systemc/kernel_backend_execution.hpp"
#include "fsim/systemc/kernel_backend_inventory.hpp"
#include "fsim/systemc/kernel_backend_observation.hpp"
#include "fsim/systemc/kernel_backend_protocol.hpp"
#include "fsim/systemc/kernel_backend_tlm1.hpp"
#include "fsim/systemc/kernel_backend_tlm2.hpp"

#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace fsim;

struct Identities {
    systemc::SystemCIslandId island;
    systemc::SystemCHierarchyId hierarchy;
    systemc::SystemCObjectId object;
    systemc::SystemCEndpointId endpoint;
    systemc::SystemCSequenceId sequence;
    systemc::SystemCTransactionId transaction;
};

class EchoBackend final : public systemc::SystemCKernelBackend {
public:
    systemc::SystemCKernelTransportResult exchange(
        const std::span<const std::byte> request) noexcept override
    {
        if (closed_) {
            return { systemc::SystemCKernelTransportStatus::disconnected, { } };
        }
        return { systemc::SystemCKernelTransportStatus::ok,
            std::vector<std::byte> { request.begin(), request.end() } };
    }

    void close() noexcept override { closed_ = true; }

private:
    bool closed_ { };
};

Identities make_identities(const systemc::SystemCKernelProtocolLimits& limits)
{
    diagnostic::Engine diagnostics;
    const auto island = systemc::make_systemc_island_id(
        "design-sha256:root:work.top", limits, diagnostics);
    const auto hierarchy = systemc::make_systemc_hierarchy_id(
        *island, "work.top.u_model", limits, diagnostics);
    const auto object = systemc::make_systemc_object_id(
        *hierarchy, "work.top.u_model.signal", limits, diagnostics);
    const auto endpoint = systemc::make_systemc_endpoint_id(
        *object, "output:value", limits, diagnostics);
    const auto sequence = systemc::make_systemc_sequence_id(*island, 17U, diagnostics);
    const auto transaction = systemc::make_systemc_transaction_id(
        *endpoint, *sequence, diagnostics);
    assert(island && hierarchy && object && endpoint && sequence && transaction);
    assert(!diagnostics.has_error());
    return { *island, *hierarchy, *object, *endpoint, *sequence, *transaction };
}

systemc::SystemCKernelMessage message_for(
    const systemc::SystemCKernelOperation operation,
    const Identities& ids)
{
    systemc::SystemCKernelMessage message;
    message.header.operation = operation;
    message.header.sequence = ids.sequence;
    message.payload = { std::byte { 0x01 }, std::byte { 0x7f }, std::byte { 0xff } };
    if (operation == systemc::SystemCKernelOperation::handshake) {
        return message;
    }
    message.header.island = ids.island;
    if (operation == systemc::SystemCKernelOperation::create_object) {
        message.header.hierarchy = ids.hierarchy;
        message.header.object = ids.object;
    }
    if (operation == systemc::SystemCKernelOperation::bind_endpoint
        || operation == systemc::SystemCKernelOperation::apply_inputs
        || operation == systemc::SystemCKernelOperation::drain_outputs
        || operation == systemc::SystemCKernelOperation::observe_transaction) {
        message.header.hierarchy = ids.hierarchy;
        message.header.object = ids.object;
        message.header.endpoint = ids.endpoint;
    }
    if (operation == systemc::SystemCKernelOperation::observe_transaction) {
        message.header.transaction = ids.transaction;
    }
    return message;
}

bool has_code(const diagnostic::Engine& diagnostics,
    const std::string_view code)
{
    return std::ranges::any_of(
        diagnostics.diagnostics(),
        [&](const auto& diagnostic) { return diagnostic.code == code; });
}

void expect_invalid(systemc::SystemCKernelMessage message,
    const systemc::SystemCKernelProtocolLimits& limits,
    const std::string_view code = "FSIM-SC-B002")
{
    diagnostic::Engine diagnostics;
    assert(!systemc::validate_systemc_kernel_message(
        message, limits, diagnostics));
    assert(has_code(diagnostics, code));
    diagnostics.clear();
    assert(!systemc::serialize_systemc_kernel_message(
        message, limits, diagnostics));
    assert(has_code(diagnostics, code));
}

} // namespace

int main()
{
    using namespace fsim;
    static_assert(sizeof(systemc::SystemCIslandId) == 16U);
    static_assert(sizeof(systemc::SystemCHierarchyId) == 16U);
    static_assert(sizeof(systemc::SystemCObjectId) == 16U);
    static_assert(sizeof(systemc::SystemCEndpointId) == 16U);
    static_assert(sizeof(systemc::SystemCTransactionId) == 16U);
    static_assert(sizeof(systemc::SystemCSequenceId) == 8U);
    static_assert(!std::is_convertible_v<systemc::SystemCObjectId,
        systemc::SystemCEndpointId>);
    static_assert(!std::is_pointer_v<systemc::SystemCIslandId>);
    static_assert(std::is_abstract_v<systemc::SystemCKernelBackend>);

    const systemc::SystemCKernelProtocolLimits limits;
    const auto ids = make_identities(limits);
    const auto repeated = make_identities(limits);
    assert(ids.island == repeated.island);
    assert(ids.hierarchy == repeated.hierarchy);
    assert(ids.object == repeated.object);
    assert(ids.endpoint == repeated.endpoint);
    assert(ids.sequence == repeated.sequence);
    assert(ids.transaction == repeated.transaction);
    assert(ids.island.valid() && ids.hierarchy.valid() && ids.object.valid());
    assert(ids.endpoint.valid() && ids.sequence.valid()
        && ids.transaction.valid());
    assert(ids.island.high == 0xcb1bf7d185be2025ULL);
    assert(ids.island.low == 0x90705223c4645f8aULL);
    assert((std::pair { ids.island.high, ids.island.low }
        != std::pair { ids.hierarchy.high, ids.hierarchy.low }));

    diagnostic::Engine alternate_diagnostics;
    const auto alternate_object = systemc::make_systemc_object_id(
        ids.hierarchy, "work.top.u_model.other", limits,
        alternate_diagnostics);
    const auto alternate_endpoint = systemc::make_systemc_endpoint_id(
        ids.object, "input:value", limits, alternate_diagnostics);
    const auto alternate_sequence = systemc::make_systemc_sequence_id(
        ids.island, 18U, alternate_diagnostics);
    const auto alternate_transaction = systemc::make_systemc_transaction_id(
        ids.endpoint, *alternate_sequence, alternate_diagnostics);
    assert(alternate_object && alternate_endpoint && alternate_sequence
        && alternate_transaction);
    assert(*alternate_object != ids.object);
    assert(*alternate_endpoint != ids.endpoint);
    assert(*alternate_sequence != ids.sequence);
    assert(*alternate_transaction != ids.transaction);
    assert(!alternate_diagnostics.has_error());

    constexpr std::array operations {
        systemc::SystemCKernelOperation::handshake,
        systemc::SystemCKernelOperation::create_session,
        systemc::SystemCKernelOperation::create_object,
        systemc::SystemCKernelOperation::bind_endpoint,
        systemc::SystemCKernelOperation::elaborate,
        systemc::SystemCKernelOperation::start,
        systemc::SystemCKernelOperation::apply_inputs,
        systemc::SystemCKernelOperation::advance,
        systemc::SystemCKernelOperation::next_activity,
        systemc::SystemCKernelOperation::drain_outputs,
        systemc::SystemCKernelOperation::report,
        systemc::SystemCKernelOperation::inspect,
        systemc::SystemCKernelOperation::snapshot,
        systemc::SystemCKernelOperation::teardown,
        systemc::SystemCKernelOperation::observe_transaction
    };
    std::vector<std::vector<std::byte>> encodings;
    for (const auto operation : operations) {
        auto request = message_for(operation, ids);
        diagnostic::Engine diagnostics;
        auto encoded = systemc::serialize_systemc_kernel_message(
            request, limits, diagnostics);
        assert(encoded && !diagnostics.has_error());
        assert(encoded->size()
            == systemc::kSystemCKernelMessageHeaderBytes
                + request.payload.size());
        assert((*encoded)[0] == std::byte { static_cast<unsigned char>('F') });
        assert((*encoded)[1] == std::byte { static_cast<unsigned char>('S') });
        assert((*encoded)[2] == std::byte { static_cast<unsigned char>('C') });
        assert((*encoded)[3] == std::byte { static_cast<unsigned char>('K') });
        if (operation == systemc::SystemCKernelOperation::handshake) {
            assert(support::Sha256::hex(support::Sha256::digest(*encoded))
                == "3e36d03af2c6b98c86ce50e64b82e0831ed7e485e6375c51c009e572a61a72b9");
        }
        EchoBackend backend;
        const auto transported = backend.exchange(*encoded);
        assert(transported.status == systemc::SystemCKernelTransportStatus::ok);
        assert(transported.bytes == *encoded);
        backend.close();
        assert(backend.exchange(*encoded).status
            == systemc::SystemCKernelTransportStatus::disconnected);
        auto decoded = systemc::deserialize_systemc_kernel_message(
            *encoded, limits, diagnostics);
        assert(decoded == request);
        auto repeated_encoding = systemc::serialize_systemc_kernel_message(
            *decoded, limits, diagnostics);
        assert(repeated_encoding == encoded);
        encodings.push_back(std::move(*encoded));

        auto response = request;
        response.header.direction = systemc::SystemCKernelMessageDirection::response;
        response.header.status = systemc::SystemCKernelMessageStatus::ok;
        response.header.sequence = *alternate_sequence;
        response.header.correlation = request.header.sequence;
        assert(systemc::validate_systemc_kernel_message(
            response, limits, diagnostics));

        auto event = request;
        event.header.direction = systemc::SystemCKernelMessageDirection::event;
        event.header.sequence = *alternate_sequence;
        assert(systemc::validate_systemc_kernel_message(
            event, limits, diagnostics));
    }
    assert(std::adjacent_find(encodings.begin(), encodings.end())
        == encodings.end());

    systemc::SystemCKernelValue crossing;
    crossing.kind = systemc::SystemCKernelValueKind::logic9;
    crossing.width = 129U;
    crossing.range = {
        64, -64, systemc::SystemCKernelRangeDirection::descending
    };
    crossing.type_name = "ieee.std_logic_1164.std_logic_vector";
    crossing.planes.assign(4U, std::vector<std::uint64_t>(3U));
    for (std::uint32_t bit = 0U; bit < crossing.width; ++bit) {
        const auto code = bit % 9U;
        const auto word = static_cast<std::size_t>(bit / 64U);
        const auto mask = std::uint64_t { 1U } << (bit % 64U);
        for (unsigned plane = 0U; plane < 4U; ++plane) {
            if ((code & (1U << plane)) != 0U) {
                crossing.planes[plane][word] |= mask;
            }
        }
    }
    systemc::SystemCKernelScalarValue crossing_scalar;
    crossing_scalar.typed = crossing;
    diagnostic::Engine crossing_diagnostics;
    const auto crossing_payload = systemc::serialize_systemc_apply_inputs_payload(
        { crossing_scalar }, { }, crossing_diagnostics);
    assert(crossing_payload && !crossing_diagnostics.has_error());
    auto crossing_message = message_for(
        systemc::SystemCKernelOperation::apply_inputs, ids);
    crossing_message.payload = *crossing_payload;
    const auto crossing_message_bytes = systemc::serialize_systemc_kernel_message(
        crossing_message, limits, crossing_diagnostics);
    assert(crossing_message_bytes && !crossing_diagnostics.has_error());
    const auto decoded_crossing_message = systemc::deserialize_systemc_kernel_message(
        *crossing_message_bytes, limits, crossing_diagnostics);
    assert(decoded_crossing_message);
    const auto decoded_crossing = systemc::deserialize_systemc_apply_inputs_payload(
        decoded_crossing_message->payload, { }, crossing_diagnostics);
    assert(decoded_crossing && decoded_crossing->value.typed == crossing);

    systemc::SystemCKernelTlm1Transaction tlm1_transaction;
    tlm1_transaction.transaction = ids.transaction;
    tlm1_transaction.endpoint = ids.endpoint;
    tlm1_transaction.peer = *alternate_endpoint;
    tlm1_transaction.sequence = ids.sequence;
    tlm1_transaction.operation
        = systemc::SystemCKernelTlm1Operation::transport;
    tlm1_transaction.state = systemc::SystemCKernelTlm1State::completed;
    tlm1_transaction.time_fs = 2000U;
    tlm1_transaction.delta = 4U;
    tlm1_transaction.explicit_bridge = true;
    tlm1_transaction.request = crossing;
    tlm1_transaction.response = crossing;
    const auto tlm1_payload
        = systemc::serialize_systemc_kernel_tlm1_transaction(
            tlm1_transaction, { }, crossing_diagnostics);
    assert(tlm1_payload && !crossing_diagnostics.has_error());
    auto tlm1_message = message_for(
        systemc::SystemCKernelOperation::observe_transaction, ids);
    tlm1_message.payload = *tlm1_payload;
    const auto tlm1_bytes = systemc::serialize_systemc_kernel_message(
        tlm1_message, limits, crossing_diagnostics);
    assert(tlm1_bytes && !crossing_diagnostics.has_error());
    const auto decoded_tlm1_message
        = systemc::deserialize_systemc_kernel_message(
            *tlm1_bytes, limits, crossing_diagnostics);
    assert(decoded_tlm1_message);
    const auto decoded_tlm1
        = systemc::deserialize_systemc_kernel_tlm1_transaction(
            decoded_tlm1_message->payload, { }, crossing_diagnostics);
    assert(decoded_tlm1 == tlm1_transaction);

    systemc::SystemCKernelTlm2Transaction tlm2_transaction;
    tlm2_transaction.transaction = ids.transaction;
    tlm2_transaction.endpoint = ids.endpoint;
    tlm2_transaction.peer = *alternate_endpoint;
    tlm2_transaction.sequence = ids.sequence;
    tlm2_transaction.operation
        = systemc::SystemCKernelTlm2Operation::nonblocking_forward;
    tlm2_transaction.phase = systemc::SystemCKernelTlm2Phase::begin_response;
    tlm2_transaction.sync = systemc::SystemCKernelTlm2Sync::completed;
    tlm2_transaction.state = systemc::SystemCKernelTlm2State::completed;
    tlm2_transaction.time_fs = 3000U;
    tlm2_transaction.delta = 5U;
    tlm2_transaction.delay_fs = 1000U;
    tlm2_transaction.explicit_bridge = true;
    tlm2_transaction.payload.address = 0x80U;
    tlm2_transaction.payload.command
        = systemc::SystemCKernelTlm2Command::write;
    tlm2_transaction.payload.response
        = systemc::SystemCKernelTlm2Response::ok;
    tlm2_transaction.payload.streaming_width = 4U;
    tlm2_transaction.payload.data = { std::byte { 1U }, std::byte { 2U },
        std::byte { 3U }, std::byte { 4U } };
    tlm2_transaction.payload.extensions = {
        { "protocol.route", { std::byte { 23U } } }
    };
    const auto tlm2_payload
        = systemc::serialize_systemc_kernel_tlm2_transaction(
            tlm2_transaction, { }, crossing_diagnostics);
    assert(tlm2_payload && !crossing_diagnostics.has_error());
    auto tlm2_message = message_for(
        systemc::SystemCKernelOperation::observe_transaction, ids);
    tlm2_message.payload = *tlm2_payload;
    const auto tlm2_bytes = systemc::serialize_systemc_kernel_message(
        tlm2_message, limits, crossing_diagnostics);
    assert(tlm2_bytes && !crossing_diagnostics.has_error());
    const auto decoded_tlm2_message
        = systemc::deserialize_systemc_kernel_message(
            *tlm2_bytes, limits, crossing_diagnostics);
    assert(decoded_tlm2_message);
    const auto decoded_tlm2
        = systemc::deserialize_systemc_kernel_tlm2_transaction(
            decoded_tlm2_message->payload, { }, crossing_diagnostics);
    assert(decoded_tlm2 == tlm2_transaction);

    systemc::SystemCKernelChannelInventory inventory {
        ids.island, ids.hierarchy
    };
    assert(inventory.register_channel(
        { "work.top.signal", "sc_signal:bool:1",
            systemc::SystemCKernelChannelKind::signal,
            systemc::SystemCKernelChannelValueProfile {
                systemc::SystemCKernelValueKind::bit2, 1U, false },
            systemc::SystemCKernelWriterPolicy::one,
            systemc::SystemCKernelUpdateOwner::signal_kernel,
            systemc::SystemCKernelObservationMode::value_changed, true },
        crossing_diagnostics));
    assert(inventory.freeze(crossing_diagnostics));
    const auto inventory_payload
        = systemc::serialize_systemc_kernel_channel_inventory(
            inventory.snapshot(), { }, crossing_diagnostics);
    assert(inventory_payload && !crossing_diagnostics.has_error());
    auto inventory_message
        = message_for(systemc::SystemCKernelOperation::snapshot, ids);
    inventory_message.payload = *inventory_payload;
    const auto inventory_bytes = systemc::serialize_systemc_kernel_message(
        inventory_message, limits, crossing_diagnostics);
    assert(inventory_bytes && !crossing_diagnostics.has_error());
    const auto decoded_inventory_message
        = systemc::deserialize_systemc_kernel_message(
            *inventory_bytes, limits, crossing_diagnostics);
    assert(decoded_inventory_message);
    const auto decoded_inventory
        = systemc::deserialize_systemc_kernel_channel_inventory(
            decoded_inventory_message->payload, { }, crossing_diagnostics);
    assert(decoded_inventory == inventory.snapshot());

    systemc::SystemCKernelBindingInventory bindings { inventory.snapshot() };
    systemc::SystemCKernelBindingTarget binding_target;
    binding_target.chain = { "work.top.input", "work.top.signal" };
    binding_target.final_channel_path = "work.top.signal";
    binding_target.foreign_language = systemc::SystemCKernelHostLanguage::vhdl;
    binding_target.foreign_endpoint = *alternate_endpoint;
    assert(bindings.register_binding(
        { "work.top.input", "sc_in", systemc::SystemCKernelBindingKind::port,
            systemc::SystemCKernelBindingDirection::input,
            { std::move(binding_target) } },
        crossing_diagnostics));
    assert(bindings.freeze(crossing_diagnostics));
    const auto binding_payload
        = systemc::serialize_systemc_kernel_binding_inventory(
            bindings.snapshot(), { }, crossing_diagnostics);
    assert(binding_payload && !crossing_diagnostics.has_error());
    auto binding_message
        = message_for(systemc::SystemCKernelOperation::snapshot, ids);
    binding_message.payload = *binding_payload;
    const auto binding_bytes = systemc::serialize_systemc_kernel_message(
        binding_message, limits, crossing_diagnostics);
    assert(binding_bytes && !crossing_diagnostics.has_error());
    const auto decoded_binding_message
        = systemc::deserialize_systemc_kernel_message(
            *binding_bytes, limits, crossing_diagnostics);
    assert(decoded_binding_message);
    const auto decoded_bindings
        = systemc::deserialize_systemc_kernel_binding_inventory(
            decoded_binding_message->payload, { }, crossing_diagnostics);
    assert(decoded_bindings == bindings.snapshot());

    systemc::SystemCKernelValue trace_value;
    trace_value.kind = systemc::SystemCKernelValueKind::logic4;
    trace_value.width = 257U;
    trace_value.range = {
        256, 0, systemc::SystemCKernelRangeDirection::descending
    };
    trace_value.type_name = "sc_lv:257";
    trace_value.planes.assign(2U, std::vector<std::uint64_t>(5U));
    for (std::size_t word = 0U; word < 5U; ++word) {
        trace_value.planes[0][word] = 0x5555555555555555ULL;
        trace_value.planes[1][word] = 0x3333333333333333ULL;
    }
    trace_value.planes[0].back() &= 1U;
    trace_value.planes[1].back() &= 1U;
    const auto trace_sequence = systemc::make_systemc_sequence_id(
        ids.island, 18U, crossing_diagnostics)
                                    .value();
    systemc::SystemCKernelExecutionReceipt trace_receipt;
    trace_receipt.session_state = systemc::SystemCKernelSessionState::quiescent;
    trace_receipt.status = systemc::SystemCKernelExecutionStatus::quiescent;
    trace_receipt.published = true;
    trace_receipt.order = { 8U, 3U,
        systemc::SystemCAccelleraRegion::quiescent, ids.island,
        trace_sequence };
    systemc::SystemCKernelScalarValue trace_crossing;
    trace_crossing.typed = trace_value;
    trace_receipt.samples = { { inventory.snapshot().channels.front().channel,
        { 8U, 2U, systemc::SystemCAccelleraRegion::update, ids.island,
            ids.sequence },
        trace_crossing, true } };
    trace_receipt.detail = "lossless repeated SystemC trace dirty batch";
    const auto trace_payload = systemc::serialize_systemc_execution_receipt(
        trace_receipt, { }, crossing_diagnostics);
    assert(trace_payload && !crossing_diagnostics.has_error());
    auto trace_message
        = message_for(systemc::SystemCKernelOperation::drain_outputs, ids);
    trace_message.header.sequence = trace_sequence;
    trace_message.payload = *trace_payload;
    const auto trace_bytes = systemc::serialize_systemc_kernel_message(
        trace_message, limits, crossing_diagnostics);
    assert(trace_bytes && !crossing_diagnostics.has_error());
    const auto decoded_trace_message
        = systemc::deserialize_systemc_kernel_message(
            *trace_bytes, limits, crossing_diagnostics);
    assert(decoded_trace_message);
    const auto decoded_trace = systemc::deserialize_systemc_execution_receipt(
        decoded_trace_message->payload, { }, crossing_diagnostics);
    assert(decoded_trace == trace_receipt);
    const auto repeated_trace = systemc::serialize_systemc_execution_receipt(
        *decoded_trace, { }, crossing_diagnostics);
    assert(repeated_trace == trace_payload);

    systemc::SystemCKernelObservationBatch observations;
    observations.island = ids.island;
    observations.records = {
        { systemc::SystemCKernelObservationKind::tlm1_end,
            { tlm1_transaction.time_fs, tlm1_transaction.delta,
                systemc::SystemCAccelleraRegion::quiescent, ids.island,
                ids.sequence },
            tlm1_transaction.endpoint, tlm1_transaction.peer,
            tlm1_transaction.transaction, std::nullopt, *tlm1_payload,
            "correlated native TLM1 completion" },
        { systemc::SystemCKernelObservationKind::tlm2_end,
            { tlm2_transaction.time_fs, tlm2_transaction.delta,
                systemc::SystemCAccelleraRegion::quiescent, ids.island,
                ids.sequence },
            tlm2_transaction.endpoint, tlm2_transaction.peer,
            tlm2_transaction.transaction, std::nullopt, *tlm2_payload,
            "correlated native TLM2 completion" }
    };
    const auto observation_payload
        = systemc::serialize_systemc_kernel_observation_batch(
            observations, { }, crossing_diagnostics);
    assert(observation_payload && !crossing_diagnostics.has_error());
    auto observation_message = message_for(
        systemc::SystemCKernelOperation::observe_transaction, ids);
    observation_message.payload = *observation_payload;
    const auto observation_message_bytes
        = systemc::serialize_systemc_kernel_message(
            observation_message, limits, crossing_diagnostics);
    assert(observation_message_bytes && !crossing_diagnostics.has_error());
    const auto decoded_observation_message
        = systemc::deserialize_systemc_kernel_message(
            *observation_message_bytes, limits, crossing_diagnostics);
    assert(decoded_observation_message);
    const auto decoded_observations
        = systemc::deserialize_systemc_kernel_observation_batch(
            decoded_observation_message->payload, { }, crossing_diagnostics);
    assert(decoded_observations == observations);
    assert(decoded_observations->records.front().transaction
        == tlm1_transaction.transaction);
    assert(!decoded_observations->records.front().value);

    auto invalid = message_for(systemc::SystemCKernelOperation::start, ids);
    invalid.header.schema = 2U;
    expect_invalid(invalid, limits);
    invalid = message_for(systemc::SystemCKernelOperation::start, ids);
    invalid.header.operation = static_cast<systemc::SystemCKernelOperation>(99U);
    expect_invalid(invalid, limits);
    invalid = message_for(systemc::SystemCKernelOperation::start, ids);
    invalid.header.direction = static_cast<systemc::SystemCKernelMessageDirection>(0U);
    expect_invalid(invalid, limits);
    invalid = message_for(systemc::SystemCKernelOperation::start, ids);
    invalid.header.flags = 1U << 8U;
    expect_invalid(invalid, limits);
    invalid = message_for(systemc::SystemCKernelOperation::start, ids);
    invalid.header.sequence = { };
    expect_invalid(invalid, limits);
    invalid = message_for(systemc::SystemCKernelOperation::start, ids);
    invalid.header.status = systemc::SystemCKernelMessageStatus::ok;
    expect_invalid(invalid, limits);
    invalid = message_for(systemc::SystemCKernelOperation::start, ids);
    invalid.header.correlation = ids.sequence;
    expect_invalid(invalid, limits);
    invalid = message_for(systemc::SystemCKernelOperation::start, ids);
    invalid.header.direction = systemc::SystemCKernelMessageDirection::response;
    expect_invalid(invalid, limits);
    invalid = message_for(systemc::SystemCKernelOperation::start, ids);
    invalid.header.direction = systemc::SystemCKernelMessageDirection::event;
    invalid.header.status = systemc::SystemCKernelMessageStatus::failed;
    expect_invalid(invalid, limits);
    invalid = message_for(systemc::SystemCKernelOperation::handshake, ids);
    invalid.header.island = ids.island;
    expect_invalid(invalid, limits);
    invalid = message_for(systemc::SystemCKernelOperation::start, ids);
    invalid.header.island = { };
    expect_invalid(invalid, limits);
    invalid = message_for(systemc::SystemCKernelOperation::create_object, ids);
    invalid.header.hierarchy = { };
    expect_invalid(invalid, limits);
    invalid = message_for(systemc::SystemCKernelOperation::bind_endpoint, ids);
    invalid.header.endpoint = { };
    expect_invalid(invalid, limits);
    invalid = message_for(
        systemc::SystemCKernelOperation::observe_transaction, ids);
    invalid.header.transaction = { };
    expect_invalid(invalid, limits);

    auto tiny_limits = limits;
    tiny_limits.max_payload_bytes = 2U;
    tiny_limits.max_message_bytes = systemc::kSystemCKernelMessageHeaderBytes + 2U;
    expect_invalid(message_for(systemc::SystemCKernelOperation::start, ids),
        tiny_limits, "FSIM-SC-B003");
    auto inconsistent_limits = limits;
    inconsistent_limits.max_message_bytes = 1U;
    expect_invalid(message_for(systemc::SystemCKernelOperation::start, ids),
        inconsistent_limits, "FSIM-SC-B003");

    diagnostic::Engine identity_diagnostics;
    auto identity_limits = limits;
    identity_limits.max_identity_bytes = 4U;
    assert(!systemc::make_systemc_island_id(
        "too-long", identity_limits, identity_diagnostics));
    assert(has_code(identity_diagnostics, "FSIM-SC-B001"));
    identity_diagnostics.clear();
    assert(!systemc::make_systemc_island_id(
        std::string_view { "bad\0id", 6U }, limits, identity_diagnostics));
    assert(has_code(identity_diagnostics, "FSIM-SC-B001"));
    identity_diagnostics.clear();
    assert(!systemc::make_systemc_hierarchy_id(
        { }, "top", limits, identity_diagnostics));
    assert(has_code(identity_diagnostics, "FSIM-SC-B001"));
    identity_diagnostics.clear();
    assert(!systemc::make_systemc_sequence_id(
        ids.island, 0U, identity_diagnostics));
    assert(!systemc::make_systemc_transaction_id(
        { }, ids.sequence, identity_diagnostics));
    assert(has_code(identity_diagnostics, "FSIM-SC-B001"));

    auto valid = message_for(systemc::SystemCKernelOperation::start, ids);
    diagnostic::Engine encoding_diagnostics;
    auto encoded = systemc::serialize_systemc_kernel_message(
        valid, limits, encoding_diagnostics);
    assert(encoded);
    auto bad_magic = *encoded;
    bad_magic[0] = std::byte { 0U };
    assert(!systemc::deserialize_systemc_kernel_message(
        bad_magic, limits, encoding_diagnostics));
    auto truncated = *encoded;
    truncated.resize(systemc::kSystemCKernelMessageHeaderBytes - 1U);
    diagnostic::Engine truncated_diagnostics;
    assert(!systemc::deserialize_systemc_kernel_message(
        truncated, limits, truncated_diagnostics));
    auto trailing = *encoded;
    trailing.push_back(std::byte { 0U });
    diagnostic::Engine trailing_diagnostics;
    assert(!systemc::deserialize_systemc_kernel_message(
        trailing, limits, trailing_diagnostics));
    auto reserved = *encoded;
    reserved[116U] = std::byte { 1U };
    diagnostic::Engine reserved_diagnostics;
    assert(!systemc::deserialize_systemc_kernel_message(
        reserved, limits, reserved_diagnostics));
    auto oversized = *encoded;
    oversized[112U] = std::byte { 0xffU };
    oversized[113U] = std::byte { 0xffU };
    oversized[114U] = std::byte { 0xffU };
    oversized[115U] = std::byte { 0x7fU };
    diagnostic::Engine oversized_diagnostics;
    assert(!systemc::deserialize_systemc_kernel_message(
        oversized, limits, oversized_diagnostics));
    assert(has_code(oversized_diagnostics, "FSIM-SC-B003"));
}
