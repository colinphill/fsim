// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_backend_transport.hpp"

#include <cassert>
#include <cstddef>
#include <string_view>
#include <vector>

namespace {

bool has_code(
    const fsim::diagnostic::Engine& diagnostics, const std::string_view code)
{
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.code == code)
            return true;
    }
    return false;
}

fsim::systemc::ScvTransportEnvelope envelope(
    const fsim::systemc::ScvIslandId island, const std::uint64_t sequence,
    const std::uint64_t time, const std::uint64_t transaction)
{
    fsim::systemc::ScvTransportEnvelope result;
    result.island = island;
    result.sequence = { sequence };
    result.record.stream = { 1U, island.low };
    result.record.generator = { 2U, island.low };
    result.record.transaction = { 3U, transaction };
    result.record.begin_time_fs = time;
    result.record.begin_delta = 1U;
    result.record.begin_region
        = fsim::runtime::TransactionRegion::evaluate;
    result.record.end_time_fs = time;
    result.record.end_delta = 1U;
    result.record.end_region = fsim::runtime::TransactionRegion::update;
    return result;
}

} // namespace

int main()
{
    using namespace fsim::systemc;
    fsim::diagnostic::Engine diagnostics;
    const ScvIslandId island_a { 0x173U, 1U };
    const ScvIslandId island_b { 0x173U, 2U };
    const auto first = envelope(island_a, 1U, 10U, 1U);

    ScvBackendRecordTransport direct(
        ScvBackendTransportKind::direct, std::nullopt);
    ScvBackendRecordTransport worker(
        ScvBackendTransportKind::worker_loopback, island_a);
    const auto direct_receipt = direct.send(first, diagnostics);
    const auto worker_receipt = worker.send(first, diagnostics);
    assert(direct_receipt.status == ScvBackendTransportStatus::accepted);
    assert(worker_receipt.status == ScvBackendTransportStatus::accepted);
    assert(direct_receipt.bytes == worker_receipt.bytes);
    assert(direct.queued_records() == 1U && direct.queued_bytes() > 64U);
    assert(worker.queued_records() == 1U);
    assert(direct.drain() == worker.drain());

    ScvBackendRecordTransport replay(
        ScvBackendTransportKind::worker_loopback, island_a);
    const auto replayed = replay.replay(direct_receipt.bytes, diagnostics);
    assert(replayed.status == ScvBackendTransportStatus::accepted);
    assert(replayed.bytes == direct_receipt.bytes);
    assert(replay.drain().front() == first);

    ScvBackendRecordTransport worker_a(
        ScvBackendTransportKind::worker_loopback, island_a);
    ScvBackendRecordTransport worker_b(
        ScvBackendTransportKind::worker_loopback, island_b);
    const auto a_late = envelope(island_a, 2U, 20U, 2U);
    const auto a_early = envelope(island_a, 1U, 10U, 3U);
    const auto b_early = envelope(island_b, 1U, 10U, 4U);
    assert(worker_a.send(a_early, diagnostics).status
        == ScvBackendTransportStatus::accepted);
    assert(worker_a.send(a_late, diagnostics).status
        == ScvBackendTransportStatus::accepted);
    assert(worker_b.send(b_early, diagnostics).status
        == ScvBackendTransportStatus::accepted);
    auto gathered = worker_a.drain();
    auto from_b = worker_b.drain();
    gathered.insert(gathered.end(), from_b.begin(), from_b.end());
    const auto merged
        = merge_scv_transport_envelopes(gathered, { }, diagnostics);
    assert(merged && merged->size() == 3U);
    assert((*merged)[0].island == island_a);
    assert((*merged)[1].island == island_b);
    assert((*merged)[2].record.begin_time_fs == 20U);

    ScvBackendRecordTransport isolated(
        ScvBackendTransportKind::worker_loopback, island_a);
    diagnostics.clear();
    assert(isolated.send(b_early, diagnostics).status
        == ScvBackendTransportStatus::rejected);
    assert(has_code(diagnostics, "FSIM-SCV-W002"));
    assert(isolated.queued_records() == 0U);
    isolated.set_connected(false);
    diagnostics.clear();
    assert(isolated.send(a_early, diagnostics).status
        == ScvBackendTransportStatus::disconnected);
    assert(has_code(diagnostics, "FSIM-SCV-W003"));
    isolated.set_connected(true);
    isolated.set_worker_crashed(true);
    diagnostics.clear();
    assert(isolated.send(a_early, diagnostics).status
        == ScvBackendTransportStatus::worker_crashed);
    assert(isolated.queued_records() == 0U);
    isolated.set_worker_crashed(false);
    assert(isolated.send(a_early, diagnostics).status
        == ScvBackendTransportStatus::accepted);

    auto narrow = ScvBackendTransportLimits { };
    narrow.max_queued_records = 1U;
    ScvBackendRecordTransport bounded(
        ScvBackendTransportKind::direct, std::nullopt, narrow);
    assert(bounded.send(a_early, diagnostics).status
        == ScvBackendTransportStatus::accepted);
    diagnostics.clear();
    assert(bounded.send(b_early, diagnostics).status
        == ScvBackendTransportStatus::backpressure);
    assert(has_code(diagnostics, "FSIM-SCV-W003"));
    assert(bounded.queued_records() == 1U);

    auto corrupt = direct_receipt.bytes;
    corrupt[0] = std::byte { 'X' };
    diagnostics.clear();
    assert(!deserialize_scv_transport_envelope(corrupt, { }, diagnostics));
    assert(has_code(diagnostics, "FSIM-SCV-W001"));
    corrupt = direct_receipt.bytes;
    corrupt[8] = std::byte { 2U };
    diagnostics.clear();
    assert(!deserialize_scv_transport_envelope(corrupt, { }, diagnostics));
    assert(has_code(diagnostics, "FSIM-SCV-W001"));

    std::vector duplicate { a_early, a_early };
    diagnostics.clear();
    assert(!merge_scv_transport_envelopes(duplicate, { }, diagnostics));
    assert(has_code(diagnostics, "FSIM-SCV-W002"));
}
