// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/transaction_record.hpp"
#include "fsim/systemc/scv_backend_transport.hpp"
#include "fsim/systemc/scv_recording.hpp"
#include "fsim/systemc/scv_resources.hpp"

#include <algorithm>
#include <cassert>
#include <vector>

int main()
{
    using namespace fsim::runtime;
    const auto make = [](const std::uint64_t time, const std::uint64_t delta,
                          const std::uint64_t sequence) {
        TransactionRecord result;
        result.stream = { 1U, 1U };
        result.generator = { 2U, 2U };
        result.transaction = { 3U, sequence };
        result.begin_time_fs = time;
        result.begin_delta = delta;
        result.begin_region = TransactionRegion::evaluate;
        result.end_time_fs = time;
        result.end_delta = delta;
        result.end_region = TransactionRegion::update;
        return result;
    };
    std::vector records { make(20U, 1U, 3U), make(10U, 2U, 2U),
        make(10U, 1U, 1U) };
    std::ranges::sort(records, transaction_record_precedes);
    assert(records[0].transaction.low == 1U);
    assert(records[1].transaction.low == 2U);
    assert(records[2].transaction.low == 3U);
    fsim::diagnostic::Engine diagnostics;
    for (const auto& record : records) {
        const auto bytes = serialize_transaction_record(
            record, { }, diagnostics);
        assert(bytes);
        assert(deserialize_transaction_record(*bytes, { }, diagnostics) == record);
    }

    using namespace fsim::systemc;
    ScvNativeRecordingRegistry native({ 0x173U, 1U }, "application-recording");
    const ScvStreamId stream { 0x173U, 2U };
    const ScvGeneratorId generator { 0x173U, 3U };
    const ScvTransactionId transaction { 0x173U, 4U };
    assert(native.create_stream(
        stream, "application.transactions", "TEST", diagnostics));
    assert(native.create_generator(generator, stream, "operation",
        "begin.value", "end.value", diagnostics));
    assert(native.begin_transaction(transaction, generator, 17,
        { 4U, TransactionRegion::evaluate }, diagnostics));
    assert(native.end_transaction(transaction, 23,
        { 4U, TransactionRegion::update }, diagnostics));
    assert(native.records().size() == 1U);
    assert(native.native_callback_count() == 2U);
    const auto native_bytes = serialize_transaction_record(
        native.records().front(), { }, diagnostics);
    assert(native_bytes);
    assert(deserialize_transaction_record(*native_bytes, { }, diagnostics)
        == native.records().front());

    ScvTransportEnvelope envelope;
    envelope.island = { 0x173U, 1U };
    envelope.sequence = { 1U };
    envelope.record = native.records().front();
    ScvBackendRecordTransport direct(
        ScvBackendTransportKind::direct, std::nullopt);
    ScvBackendRecordTransport loopback(
        ScvBackendTransportKind::worker_loopback, envelope.island);
    const auto direct_receipt = direct.send(envelope, diagnostics);
    const auto loopback_receipt = loopback.send(envelope, diagnostics);
    assert(direct_receipt.status == ScvBackendTransportStatus::accepted);
    assert(loopback_receipt.status == ScvBackendTransportStatus::accepted);
    assert(direct_receipt.bytes == loopback_receipt.bytes);
    assert(direct.drain() == loopback.drain());

    ScvResourceLimits resource_limits;
    resource_limits.max_transactions = 8U;
    resource_limits.max_attributes_per_transaction = 4U;
    resource_limits.max_queue_records = 2U;
    resource_limits.max_queue_bytes = 1024U * 1024U;
    ScvResourceWorkload resource_workload;
    resource_workload.transactions = 4U;
    resource_workload.attributes_per_transaction = 2U;
    resource_workload.producer_failure_at = 1U;
    resource_workload.consumer_failure_at = 2U;
    const auto resource_metrics = run_scv_resource_probe(
        { 0x173U, 18U }, resource_workload, resource_limits, diagnostics);
    assert(resource_metrics && !diagnostics.has_error());
    assert(resource_metrics->recorded_transactions == 4U);
    assert(resource_metrics->transported_transactions == 4U);
    assert(resource_metrics->producer_failures == 1U);
    assert(resource_metrics->consumer_failures == 1U);
    assert(resource_metrics->backpressure_events > 0U);
}
