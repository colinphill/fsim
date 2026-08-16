// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_backend_protocol.hpp"
#include "fsim/systemc/scv_resources.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace fsim::systemc;

struct Identities {
    ScvIslandId island;
    ScvHierarchyId hierarchy;
    ScvObjectId object;
    ScvStreamId stream;
    ScvGeneratorId generator;
    ScvSequenceId sequence;
    ScvTransactionId transaction;
};

Identities make_identities()
{
    ScvBackendProtocolLimits limits;
    fsim::diagnostic::Engine diagnostics;
    const auto island = make_scv_island_id("island:primary", limits, diagnostics);
    const auto hierarchy = make_scv_hierarchy_id(
        *island, "work.top", limits, diagnostics);
    const auto object = make_scv_object_id(
        *hierarchy, "work.top.controller", limits, diagnostics);
    const auto stream = make_scv_stream_id(
        *object, "requests", limits, diagnostics);
    const auto generator = make_scv_generator_id(
        *stream, "request-generator", limits, diagnostics);
    const auto sequence = make_scv_sequence_id(*island, 17U, diagnostics);
    const auto transaction = make_scv_transaction_id(
        *generator, *sequence, diagnostics);
    assert(island && hierarchy && object && stream && generator && sequence
        && transaction && !diagnostics.has_error());
    return { *island, *hierarchy, *object, *stream, *generator, *sequence,
        *transaction };
}

ScvBackendMessage message_for(
    const ScvBackendOperation operation, const Identities& ids)
{
    ScvBackendMessage message;
    message.header.operation = operation;
    message.header.sequence = ids.sequence;
    message.header.time_fs = 12000U;
    message.header.delta = 9U;
    message.header.region = ScvBackendRegion::evaluate;
    message.payload = { std::byte { 0x01U }, std::byte { 0x7fU },
        std::byte { 0xffU } };
    switch (operation) {
    case ScvBackendOperation::handshake:
        message.header.time_fs = 0U;
        message.header.delta = 0U;
        message.header.region = ScvBackendRegion::none;
        break;
    case ScvBackendOperation::flush:
        message.header.island = ids.island;
        break;
    case ScvBackendOperation::randomize:
    case ScvBackendOperation::inspect:
        message.header.island = ids.island;
        message.header.hierarchy = ids.hierarchy;
        message.header.object = ids.object;
        break;
    case ScvBackendOperation::create_stream:
        message.header.island = ids.island;
        message.header.hierarchy = ids.hierarchy;
        message.header.object = ids.object;
        message.header.stream = ids.stream;
        break;
    case ScvBackendOperation::create_generator:
        message.header.island = ids.island;
        message.header.hierarchy = ids.hierarchy;
        message.header.object = ids.object;
        message.header.stream = ids.stream;
        message.header.generator = ids.generator;
        break;
    case ScvBackendOperation::begin_transaction:
    case ScvBackendOperation::record_attribute:
    case ScvBackendOperation::relate_transaction:
    case ScvBackendOperation::end_transaction:
        message.header.island = ids.island;
        message.header.hierarchy = ids.hierarchy;
        message.header.object = ids.object;
        message.header.stream = ids.stream;
        message.header.generator = ids.generator;
        message.header.transaction = ids.transaction;
        break;
    }
    return message;
}

bool has_code(
    const fsim::diagnostic::Engine& diagnostics,
    const std::string_view code)
{
    return std::ranges::any_of(
        diagnostics.diagnostics(), [&](const auto& diagnostic) {
            return diagnostic.code == code;
        });
}

void expect_invalid(ScvBackendMessage message)
{
    ScvBackendProtocolLimits limits;
    fsim::diagnostic::Engine diagnostics;
    assert(!validate_scv_backend_message(message, limits, diagnostics));
    assert(has_code(diagnostics, "FSIM-SCV-B002")
        || has_code(diagnostics, "FSIM-SCV-B003"));
}

} // namespace

int main()
{
    static_assert(sizeof(ScvIslandId) == 16U);
    static_assert(sizeof(ScvHierarchyId) == 16U);
    static_assert(sizeof(ScvObjectId) == 16U);
    static_assert(sizeof(ScvStreamId) == 16U);
    static_assert(sizeof(ScvGeneratorId) == 16U);
    static_assert(sizeof(ScvTransactionId) == 16U);
    const ScvResourceLimits resource_limits;
    assert(resource_limits.max_transactions == 1024U * 1024U);
    assert(resource_limits.max_queue_bytes == 64U * 1024U * 1024U);
    assert(resource_limits.max_solver_search_steps == 1024U * 1024U);

    const auto ids = make_identities();
    const auto repeated = make_identities();
    assert(ids.island == repeated.island);
    assert(ids.hierarchy == repeated.hierarchy);
    assert(ids.object == repeated.object);
    assert(ids.stream == repeated.stream);
    assert(ids.generator == repeated.generator);
    assert(ids.transaction == repeated.transaction);

    ScvBackendProtocolLimits limits;
    fsim::diagnostic::Engine alternate_diagnostics;
    const auto alternate_hierarchy = make_scv_hierarchy_id(
        ids.island, "work.other", limits, alternate_diagnostics);
    const auto alternate_stream = make_scv_stream_id(
        ids.object, "responses", limits, alternate_diagnostics);
    const auto alternate_sequence = make_scv_sequence_id(
        ids.island, 18U, alternate_diagnostics);
    const auto alternate_transaction = make_scv_transaction_id(
        ids.generator, *alternate_sequence, alternate_diagnostics);
    assert(alternate_hierarchy && alternate_stream && alternate_sequence
        && alternate_transaction && !alternate_diagnostics.has_error());
    assert(*alternate_hierarchy != ids.hierarchy);
    assert(*alternate_stream != ids.stream);
    assert(*alternate_transaction != ids.transaction);
    assert(ids.object.high != ids.stream.high || ids.object.low != ids.stream.low);

    constexpr std::array operations {
        ScvBackendOperation::handshake,
        ScvBackendOperation::create_stream,
        ScvBackendOperation::create_generator,
        ScvBackendOperation::randomize,
        ScvBackendOperation::inspect,
        ScvBackendOperation::begin_transaction,
        ScvBackendOperation::record_attribute,
        ScvBackendOperation::relate_transaction,
        ScvBackendOperation::end_transaction,
        ScvBackendOperation::flush
    };
    for (const auto operation : operations) {
        const auto request = message_for(operation, ids);
        fsim::diagnostic::Engine encode_diagnostics;
        const auto encoded = serialize_scv_backend_message(
            request, limits, encode_diagnostics);
        assert(encoded && encoded->size() == scv_backend_message_header_bytes + request.payload.size());
        assert(!encode_diagnostics.has_error());
        fsim::diagnostic::Engine decode_diagnostics;
        assert(deserialize_scv_backend_message(
                   *encoded, limits, decode_diagnostics)
            == request);
        assert(!decode_diagnostics.has_error());
        fsim::diagnostic::Engine repeat_diagnostics;
        assert(serialize_scv_backend_message(
                   request, limits, repeat_diagnostics)
            == encoded);
    }

    auto receipt = message_for(ScvBackendOperation::end_transaction, ids);
    receipt.header.direction = ScvBackendDirection::receipt;
    receipt.header.status = ScvBackendStatus::ok;
    receipt.header.sequence = ScvSequenceId { 18U };
    receipt.header.correlation = ids.sequence;
    receipt.header.time_fs = 13000U;
    receipt.header.delta = 11U;
    receipt.header.region = ScvBackendRegion::postponed;
    fsim::diagnostic::Engine receipt_diagnostics;
    const auto receipt_bytes = serialize_scv_backend_message(
        receipt, limits, receipt_diagnostics);
    const auto decoded_receipt = deserialize_scv_backend_message(
        *receipt_bytes, limits, receipt_diagnostics);
    assert(decoded_receipt == receipt && !receipt_diagnostics.has_error());
    assert(decoded_receipt->header.time_fs == 13000U);
    assert(decoded_receipt->header.delta == 11U);
    assert(decoded_receipt->header.region == ScvBackendRegion::postponed);
    assert(decoded_receipt->header.correlation == ids.sequence);

    fsim::diagnostic::Engine identity_diagnostics;
    assert(!make_scv_island_id({ }, limits, identity_diagnostics));
    assert(!make_scv_island_id(" leading", limits, identity_diagnostics));
    assert(!make_scv_hierarchy_id(
        { }, "work.top", limits, identity_diagnostics));
    assert(!make_scv_object_id(
        ids.hierarchy, "work\\top", limits, identity_diagnostics));
    assert(!make_scv_stream_id(
        ids.object, std::string(4097U, 's'), limits, identity_diagnostics));
    assert(!make_scv_generator_id(
        { }, "generator", limits, identity_diagnostics));
    assert(!make_scv_sequence_id(ids.island, 0U, identity_diagnostics));
    assert(!make_scv_transaction_id(
        ids.generator, { }, identity_diagnostics));
    assert(has_code(identity_diagnostics, "FSIM-SCV-B001"));

    auto invalid = message_for(ScvBackendOperation::create_stream, ids);
    invalid.header.schema = 2U;
    expect_invalid(invalid);
    invalid = message_for(ScvBackendOperation::create_stream, ids);
    invalid.header.flags = 1U << 31U;
    expect_invalid(invalid);
    invalid = message_for(ScvBackendOperation::create_stream, ids);
    invalid.header.region = static_cast<ScvBackendRegion>(99U);
    expect_invalid(invalid);
    invalid = message_for(ScvBackendOperation::create_stream, ids);
    invalid.header.generator = ids.generator;
    expect_invalid(invalid);
    invalid = message_for(ScvBackendOperation::randomize, ids);
    invalid.header.object = { };
    expect_invalid(invalid);
    invalid = message_for(ScvBackendOperation::flush, ids);
    invalid.header.sequence = { };
    expect_invalid(invalid);
    invalid = message_for(ScvBackendOperation::flush, ids);
    invalid.header.status = ScvBackendStatus::ok;
    expect_invalid(invalid);
    invalid = message_for(ScvBackendOperation::flush, ids);
    invalid.header.direction = ScvBackendDirection::receipt;
    invalid.header.status = ScvBackendStatus::ok;
    expect_invalid(invalid);

    auto oversized = message_for(ScvBackendOperation::inspect, ids);
    oversized.payload.resize(limits.max_payload_bytes + 1U);
    fsim::diagnostic::Engine oversized_diagnostics;
    assert(!serialize_scv_backend_message(
        oversized, limits, oversized_diagnostics));
    assert(has_code(oversized_diagnostics, "FSIM-SCV-B003"));

    auto bad_limits = limits;
    bad_limits.max_message_bytes = scv_backend_message_header_bytes;
    fsim::diagnostic::Engine limits_diagnostics;
    assert(!validate_scv_backend_message(
        message_for(ScvBackendOperation::flush, ids), bad_limits,
        limits_diagnostics));
    assert(has_code(limits_diagnostics, "FSIM-SCV-B003"));

    auto corrupt = *receipt_bytes;
    corrupt.front() = std::byte { 0U };
    fsim::diagnostic::Engine magic_diagnostics;
    assert(!deserialize_scv_backend_message(
        corrupt, limits, magic_diagnostics));
    assert(has_code(magic_diagnostics, "FSIM-SCV-B002"));
    corrupt = *receipt_bytes;
    corrupt[8] = std::byte { 2U };
    fsim::diagnostic::Engine schema_diagnostics;
    assert(!deserialize_scv_backend_message(
        corrupt, limits, schema_diagnostics));
    assert(has_code(schema_diagnostics, "FSIM-SCV-B002"));
    corrupt = *receipt_bytes;
    corrupt[12] = std::byte { 0xffU };
    fsim::diagnostic::Engine operation_diagnostics;
    assert(!deserialize_scv_backend_message(
        corrupt, limits, operation_diagnostics));
    assert(has_code(operation_diagnostics, "FSIM-SCV-B002"));
    corrupt = *receipt_bytes;
    corrupt[149] = std::byte { 1U };
    fsim::diagnostic::Engine reserved_diagnostics;
    assert(!deserialize_scv_backend_message(
        corrupt, limits, reserved_diagnostics));
    assert(has_code(reserved_diagnostics, "FSIM-SCV-B003"));
    corrupt = *receipt_bytes;
    corrupt[152] = std::byte { 0xffU };
    fsim::diagnostic::Engine size_diagnostics;
    assert(!deserialize_scv_backend_message(
        corrupt, limits, size_diagnostics));
    assert(has_code(size_diagnostics, "FSIM-SCV-B003"));
    corrupt = *receipt_bytes;
    corrupt.pop_back();
    fsim::diagnostic::Engine truncated_diagnostics;
    assert(!deserialize_scv_backend_message(
        corrupt, limits, truncated_diagnostics));
    assert(has_code(truncated_diagnostics, "FSIM-SCV-B003"));
    corrupt = *receipt_bytes;
    corrupt.push_back(std::byte { 0U });
    fsim::diagnostic::Engine trailing_diagnostics;
    assert(!deserialize_scv_backend_message(
        corrupt, limits, trailing_diagnostics));
    assert(has_code(trailing_diagnostics, "FSIM-SCV-B003"));
}
