// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/transaction_record.hpp"
#include "fsim/systemc/scv_constraints.hpp"
#include "fsim/systemc/scv_extensions.hpp"
#include "fsim/systemc/scv_random.hpp"
#include "fsim/systemc/scv_recording.hpp"

#include <array>
#include <cassert>
#include <cstdint>

int main()
{
    using namespace fsim::systemc;
    fsim::diagnostic::Engine diagnostics;
    const auto baseline = ScvNativeSmartPtrRegistry::live_native_payloads();
    const ScvIslandId island { 0x173U, 1U };
    const ScvObjectId object { 0x173U, 2U };
    const auto seeds = derive_scv_random_seeds(
        17317U, island, object, "corpus.main", 0U, { }, diagnostics);
    assert(seeds);
    ScvRandomStream first_random(seeds->thread);
    ScvRandomStream replay_random(seeds->thread);
    for (std::size_t index = 0U; index < 32U; ++index)
        assert(first_random.next(diagnostics) == replay_random.next(diagnostics));

    {
        ScvNativeSmartPtrRegistry native;
        const auto aggregate = native.create(ScvNativeValueKind::aggregate,
            object, "corpus.aggregate", seeds->object, diagnostics);
        assert(aggregate);
        const std::vector<ScvNativeExtensionStep> scalar_path {
            { ScvNativeExtensionStepKind::field, 0U }
        };
        ScvConstraintRequest request;
        request.variables = { { "corpus.aggregate.scalar", *aggregate,
            scalar_path, { 3, 5, 7 } } };
        request.clauses = { { "scalar-is-five", { { 0U, 1 } },
            ScvConstraintRelation::equal, 5, false } };
        request.selection = 17U;
        const auto solved = solve_scv_constraints(native, request, diagnostics);
        assert(solved.status == ScvConstraintStatus::satisfied);
        assert(solved.assignments.size() == 1U);
        assert(solved.assignments.front().value == 5);
        const auto snapshot
            = capture_scv_extensions(native, *aggregate, { }, diagnostics);
        assert(snapshot && snapshot->nodes.size() == 10U);
        assert(native.release(*aggregate, diagnostics));
    }
    assert(ScvNativeSmartPtrRegistry::live_native_payloads() == baseline);

    ScvNativeRecordingRegistry recording(island, "corpus-recording");
    const ScvStreamId stream { 0x173U, 3U };
    const ScvGeneratorId generator { 0x173U, 4U };
    const ScvTransactionId transaction { 0x173U, 5U };
    assert(recording.create_stream(
        stream, "corpus.transactions", "TEST", diagnostics));
    assert(recording.create_generator(
        generator, stream, "operation", "begin", "end", diagnostics));
    assert(recording.begin_transaction(transaction, generator, 1,
        { 0U, fsim::runtime::TransactionRegion::evaluate }, diagnostics));
    assert(recording.end_transaction(transaction, 2,
        { 0U, fsim::runtime::TransactionRegion::update }, diagnostics));
    assert(recording.records().size() == 1U);

    const auto persisted_record = fsim::runtime::serialize_transaction_record(
        recording.records().front(), { }, diagnostics);
    assert(persisted_record);
    const auto restored_record = fsim::runtime::deserialize_transaction_record(
        *persisted_record, { }, diagnostics);
    assert(restored_record && *restored_record == recording.records().front());
    assert(recording.release_transaction(transaction, diagnostics));
    assert(recording.live_handles() == 0U);
    assert(!diagnostics.has_error());
}
